#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <process.h>
#include <cstdio>
#include <math.h>

#pragma warning(disable:4996)
#pragma comment(lib,"ws2_32.lib")
#define SERVER_PORT 12341 //端口号
#define CLIENT_PORT 12340 //端口号
#define SERVER_IP "127.0.0.1" //IP 地址
#define CLIENT_IP "127.0.0.1" //客户端IP
const int BUFFER_LENGTH = 1026;
const int SEND_WIND_SIZE = 4;
const int SEQ_SIZE = 20;//接收端序列号个数，为 1~20

#define DATA_SIZE 1024

int ack[SEQ_SIZE + 1]; //收到 ack 情况，对应 0~19 的 ack, 0 表示还没发，1 表示发了没收到， 2 表示收到了

int cache_lengths[SEQ_SIZE + 1];
int counter[SEQ_SIZE + 1];//计时器，当为负数时表示未启动，为正数时表示启动；其他同GBN
int curSeq;//当前数据包的 seq
int curAck;//当前等待确认的 ack
int totalSeq;//收到的包的总数
int totalPacket;//需要发送的包总数

unsigned int __stdcall ProxyThread(LPVOID lpParameter);


void getCurTime(char* ptime) {
	SYSTEMTIME sys;
	GetLocalTime(&sys);
	sprintf_s(ptime, 20, "%d-%d-%d %d:%d:%d", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond);
}

BOOL lossInLossRatio(float lossRatio) { 
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

bool seqIsAvailable() {
	int step = curSeq - curAck;
	if (step < 0) {
		step += SEQ_SIZE;
	}
	//printf("ack[%d] = %d\n", curSeq, ack[curSeq]);
	return step < SEND_WIND_SIZE ? (ack[curSeq] == 0) : false;
}

void printTips()
{
	printf("\n************************************************\n");
	printf("|     -time to get current time                |\n");
	printf("|     -quit to exit client                     |\n");
	printf("|     -testsr [X] [Y] to test the sr           |\n");
	printf("************************************************\n");
}

void ackHandler(char c) {
	unsigned char index = (unsigned char)c - 1; //序列号减一
	printf("Recv a ack of %d\n", index + 1);
	if (curAck != index) {//分组失序，暂时缓存
		ack[index] = 2;
	}
	else {
		//一次分组到达，窗口向前移动（可能不止一次移动）
		//printf("index = %d\n", index);
		ack[index] = 0;
		curAck = index + 1;
		curAck %= SEQ_SIZE;
		for (int i = index + 1; i < index + SEND_WIND_SIZE; i++) {
			i %= SEQ_SIZE;
			if (ack[i] == 2) {
				counter[i] = -1;//计时器关闭
				curAck = i + 1;//修改curAck
				ack[i] = 0;
			}
			else break;
		}
		curAck %= SEQ_SIZE;
		printf("send_base move to %d\n", curAck);
	}
}

void click() {
	for (int i = 0; i < SEQ_SIZE; i++) {
		if (counter[i] >= 0) {
			counter[i] += 1;
		}
	}
}

int checkTimeout() { // modify2
	for (int i = 0; i < SEQ_SIZE; i++) {
		if (counter[i] >= 5) {
			return i;
		}
	}
	return -1;
}

struct ProxyParam {
};

int main(int argc, char* argv[])
{
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("WSAStartup failed with error: %d\n", err);
		return -1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
	}
	else {
		printf("The Winsock 2.2 dll was found okay\n");
	}
	SOCKET sockServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//设置套接字为非阻塞模式
	int iMode = 1; //1：非阻塞，0：阻塞
	ioctlsocket(sockServer, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置
	SOCKADDR_IN addrServer; //服务器地址
	//addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//两者均可
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	err = bind(sockServer, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
	if (err) {
		err = GetLastError();
		printf("Could not bind the port %d for socket.Error code is % d\n", SERVER_PORT, err);
		WSACleanup();
		return -1;
	}
	SOCKADDR_IN addrClient; //客户端地址
	int length = sizeof(SOCKADDR);
	char buffer[BUFFER_LENGTH]; //数据发送接收缓冲区
	ZeroMemory(buffer, sizeof(buffer));
	//将测试数据读入内存
	std::ifstream icin;
	icin.open("client_output.txt");
	char data[DATA_SIZE * 113];//需要发送的数据
	ZeroMemory(data, sizeof(data));
	icin.read(data, DATA_SIZE * 113);
	icin.close();
	totalPacket = ceil(strlen(data) / 1024.0);
	printf("File size is %dB, each packet is 1024B and packet total num is %d\n", strlen(data), totalPacket);
	int recvSize;
	
	char cache[SEQ_SIZE + 1][DATA_SIZE + 1];//缓存，暂时保存发送但未受到ack的分组
	bool sendFlag = true;
	ProxyParam* lpProxyParam = new ProxyParam;
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
	while (true) {
		for (int i = 0; i < SEQ_SIZE; ++i) {
			ack[i] = 0;
			counter[i] = -1;
		}
		sendFlag = true;
		//非阻塞接收，若没有收到数据，返回值为-1
		recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
		if (recvSize < 0) {
			Sleep(200);
			continue;
		}
		printf("recv from client: %s\n", buffer);
		if (strcmp(buffer, "-time") == 0) {
			getCurTime(buffer);
		}
		else if (strcmp(buffer, "-quit") == 0) {
			strcpy_s(buffer, strlen("Good bye!") + 1, "Good bye!");
		}
		else if (strcmp(buffer, "-testsr") == 0) {
			printf("The send window size is %d\n", SEND_WIND_SIZE);
			printf("The available seq is 1~%d\n", SEQ_SIZE);
			ZeroMemory(buffer, sizeof(buffer));
			int recvSize;
			int waitCount = 0;
			printf("Begin to test SR protocol,please don't abort the process\n");
			printf("Shake hands stage\n");
			int stage = 0;
			bool runFlag = true;
			while (runFlag) {
				switch (stage) {
				case 0://发送 205 阶段
					buffer[0] = 205;
					buffer[1] = '\0';
					sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
					Sleep(100);
					stage = 1;
					break;
				case 1://等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
					if (recvSize < 0) {
						++waitCount;
						if (waitCount > 6) {
							runFlag = false;
							printf("[warning]Timeout error\n");
							break;
						}
						Sleep(500);
						continue;
					}
					else {
						if ((unsigned char)buffer[0] == 200) {
							printf("Begin a file transfer\n");
							printf("File size is %dB, each packet is 1024B and packet total num is %d\n", strlen(data), totalPacket);
							curSeq = 0;
							curAck = 0;
							totalSeq = 0;
							stage = 2;
						}
					}
					break;
				case 2://数据传输阶段
					if (seqIsAvailable()) {
						if (totalSeq <= totalPacket - 1) {
							//发送给客户端的序列号从 1 开始
							//printf("curSeq = %d\n", curSeq + 1);
							buffer[0] = curSeq + 1;
							ack[curSeq] = 1;
							memcpy(&buffer[1], data + DATA_SIZE * totalSeq, DATA_SIZE);
							memcpy(cache[curSeq], data + DATA_SIZE * totalSeq, DATA_SIZE);//缓存分组
							printf("send a packet with a seq of %d\n", curSeq + 1);
							//printf("buffer = %s\n", buffer);
							sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
							counter[curSeq] = 0;//计时器开启
							++curSeq;
							curSeq %= SEQ_SIZE;
							++totalSeq;
							Sleep(500);
						}
					}
					//等待 Ack，若没有收到，则返回值为-1，计数器+1
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
					if (recvSize < 0) {
						click();
						//5 次等待 ack 则超时重传 modify1
						if (checkTimeout() != -1) {
							int index = checkTimeout();
							printf("Seq %d time out.\n", index + 1);
							buffer[0] = index + 1;
							memcpy(&buffer[1], cache[index], DATA_SIZE);
							printf("Resend a packet with a seq of %d\n", index + 1);
							sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
							counter[index] = 0;//重置计时器
						}
					}
					else {
						//收到 ack
						ackHandler(buffer[0]);
						counter[buffer[0] - 1] = -1;//计时器关闭
						//数判断是否传输完成
						if (totalSeq > totalPacket - 1) {//传输完成，若都收到Ack则传输结束，否则不发送数据等待超时重传
							//puts("I'm in here！");
							bool finish = true;
							for (int i = 0; i < SEQ_SIZE; i++) {
								if (ack[i] == 1) {
									finish = false;
									break;
								}
							}
							if (finish) {
								printf("\nServer send finish!\n");
								buffer[0] = 204;
								buffer[1] = '\0';
								sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
								Sleep(100);
								runFlag = false;
								sendFlag = false;
								break;
							}
						}
					}
					Sleep(500);
					break;
				}
			}
		}
		if (sendFlag)
			sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
		printTips();
		Sleep(500);
	}
	//关闭套接字，卸载库
	closesocket(sockServer);
	WSACleanup();
	return 0;
}


unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("WSAStartup failed with error: %d\n", err);
		return 1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
	}
	else {
		printf("The Winsock 2.2 dll was found okay\n");
	}
	SOCKET socketClient = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN addrServer;
	addrServer.sin_addr.S_un.S_addr = inet_addr(CLIENT_IP);
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(CLIENT_PORT);
	//接收缓冲区
	char buffer[BUFFER_LENGTH];
	ZeroMemory(buffer, sizeof(buffer));
	int len = sizeof(SOCKADDR);
	printTips();
	int ret;//受到数据大小
	int interval = 1;//收到数据包之后返回 Ack 的间隔，默认为 1 表示每个都返回 Ack，0 或者负数均表示所有的都不返回 Ack
	char cmd[128];
	float packetLossRatio = 0.2f; //默认包丢失率 0.2
	float ackLossRatio = 0.2f; //默认 ACK 丢失率 0.2
	//用时间作为随机种子，放在循环的最外面
	srand((unsigned)time(NULL));
	std::ofstream out;
	char cache[SEQ_SIZE + 1][DATA_SIZE];//缓存，暂时保存失序但未确认的分组
	bool sendFlag = true;
	int Ack[SEQ_SIZE + 1];
	while (true) {
		sendFlag = true;
		gets_s(buffer, sizeof(buffer));
		//printf("buffer:%s\n", buffer);
		ret = sscanf_s(buffer, "%s%f%f", &cmd, sizeof(cmd), &packetLossRatio, &ackLossRatio);
		printf("buffer:%s\n", cmd);
		//printf("packet:%f\n", packetLossRatio);
		//printf("Ack:%f\n", ackLossRatio);
		if (!strcmp(cmd, "-testsr")) {
			printf("The receive window is %d\n", SEND_WIND_SIZE);
			bool runFlag = true;
			out.open("client_input.txt");
			printf("%s\n", "Begin to test SR protocol, please don't abort the process");
			printf("The loss ratio of packet is %.2f,the loss ratio of Ack is % .2f\n", packetLossRatio, ackLossRatio);
			int waitCount = 0;
			int stage = 0;
			BOOL b;
			unsigned short seq;//包的序列号
			unsigned short recvSeq;//接收窗口大小为 1，已确认的序列号
			unsigned short waitSeq;//等待的序列号
			sendto(socketClient, "-testsr", strlen("-testsr") + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
			memset(Ack, 0, sizeof(Ack));

			while (runFlag)
			{
				//等待 server 回复设置 UDP 为阻塞模式
				recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
				switch (stage) {
				case 0://等待握手阶段
					if ((unsigned char)buffer[0] == 205)
					{
						printf("Ready for file transmission\n");
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(socketClient, buffer, 2, 0,
							(SOCKADDR*)&addrServer, sizeof(SOCKADDR));
						stage = 1;
						recvSeq = 0;
						waitSeq = 1;
						for (int i = 1; i <= SEND_WIND_SIZE; i++) {
							Ack[i] = 1;
						}
					}
					break;
				case 1://等待接收数据阶段
					if ((unsigned char)buffer[0] == 204) {
						printf("\nReceive finish!\n");
						out.close();
						sendFlag = false;
						runFlag = false;
						break;
					}
					seq = (unsigned short)buffer[0];
					//随机法模拟包是否丢失
					b = lossInLossRatio(packetLossRatio);
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;
					}
					printf("recv a packet with a seq of %d\n", seq);
					if (waitSeq == seq) {
						//当前接收分组直接写入文件
						out.write(&buffer[1], strlen(&buffer[1]));
						//printf("buffer %d: %s\n", seq, &buffer[1]);
						Ack[seq] = 0;
						waitSeq += 1;
						waitSeq = (waitSeq % SEQ_SIZE == 0) ? SEQ_SIZE : waitSeq % SEQ_SIZE;
						int k = waitSeq + SEND_WIND_SIZE - 1;
						k = (k % SEQ_SIZE == 0) ? SEQ_SIZE : k % SEQ_SIZE;
						Ack[k] = 1;
						//查看是否有失序分组需要写入文件
						for (int i = waitSeq; i < waitSeq + SEND_WIND_SIZE; i++) {
							int t = (i % SEQ_SIZE == 0) ? SEQ_SIZE : i % SEQ_SIZE;
							waitSeq = t;
							//printf("i : %d, Ack[i] : %d\n", i, Ack[i]);
							if (Ack[t] == 2) {
								Ack[t] = 0;
								//printf("I'm here!!%d\n", t);
								k = t + SEND_WIND_SIZE;
								k = (k % SEQ_SIZE == 0) ? SEQ_SIZE : k % SEQ_SIZE;
								Ack[k] = 1;
								printf("seq %d will be Cached out now\n", t);
								//printf("Cache %d write in len %d: %s\n", t, cache_lengths[t], cache[t]);
								out.write(cache[t], cache_lengths[t]);
							}
							else break;
						}
						// printf("rec_base move to %d, rightest to %d\n", waitSeq, k);
						printf("rec_base move to %d\n", waitSeq);
						buffer[0] = seq;
						recvSeq = seq;
						buffer[1] = '\0';
					}
					else if ((seq > waitSeq && seq < waitSeq + SEND_WIND_SIZE) || (Ack[seq] == 1)) {//分组失序到达 modify3
						cache_lengths[seq] = strlen(&buffer[1]) + 1;
						memcpy(cache[seq], &buffer[1], cache_lengths[seq]);//缓存收到的数据，不修改下一个需要的分组序列号
						cache[seq][cache_lengths[seq]] = '\0';
						// printf("cache %d len %d buffer len %d\n", seq, strlen(cache[seq]), strlen(&buffer[1]) + 1);
						buffer[0] = seq;
						buffer[1] = '\0';
						// printf("seq %d receive, but not in order, Ack[seq] = %d\n", seq, Ack[seq]);
						printf("seq %d receive, but not in order\n", seq);
						Ack[seq] = 2;
					}
					else {
						buffer[0] = seq;
						buffer[1] = '\0';
					}
					b = lossInLossRatio(ackLossRatio);
					if (b) {
						printf("The Ack of %d loss\n", (unsigned char)buffer[0]);
						continue;
					}
					sendto(socketClient, buffer, 2, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
					printf("send a Ack of %d\n", (unsigned char)buffer[0]);
					break;
				}
				Sleep(500);
			}
		}
		if (sendFlag) {
			sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
			ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
			printf("%s\n", buffer);
			if (!strcmp(buffer, "Good bye!")) {
				break;
			}
		}
		printTips();
	}
	closesocket(socketClient);
	WSACleanup();
	return 0;
}