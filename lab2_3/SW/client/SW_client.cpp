#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <WinSock2.h>
#include <stdio.h>
#include <fstream>
#include <process.h>
#include <iostream>
#include <cstdlib>

// using namespace std;
#pragma comment(lib,"ws2_32.lib")
#pragma warning(disable : 4996)
#define SERVER_PORT 12341 //端口号
#define CLIENT_PORT 12340 //端口号
#define SERVER_IP "127.0.0.1" //IP 地址
#define CLIENT_IP "127.0.0.1" //客户端IP
const int BUFFER_LENGTH = 1026;//缓冲区大小，（以太网中 UDP 的数据帧中包长度应小于 1480 字节）
const int SEND_WIND_SIZE = 1;//发送窗口大小为 5，GBN 中应满足 W + 1 <= N（W 为发送窗口大小，N 为序列号个数）
//本例取序列号 0...19 共 20 个
//如果将窗口大小设为 1，则为停-等协议
const int SEQ_SIZE = 20; //序列号的个数，从 0~19 共计 20 个
//由于发送数据第一个字节如果值为 0，则数据会发送失败
//因此接收端序列号为 1~20，与发送端一一对应
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

BOOL ack[SEQ_SIZE];//收到 ack 情况，对应 0~19 的 ack
int curSeq;//当前数据包的 seq
int curAck;//当前等待确认的 ack
int totalSeq;//收到的包的总数
int totalPacket;//需要发送的包总数
int a = 0;

void getCurTime(char* ptime) {
	SYSTEMTIME sys;
	GetLocalTime(&sys);
	sprintf_s(ptime, 20, "%d-%d-%d %d:%d:%d", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond);
}

bool seqIsAvailable() {
	int step = curSeq - curAck;
	if (step < 0) {
		step += SEQ_SIZE;
	}
	return step < SEND_WIND_SIZE ? ack[curSeq] : false;
}


void timeoutHandler() {	// modify
	printf("Timer out error.\n");
	int index;
	for (int i = 0; i < SEND_WIND_SIZE; ++i) {
		index = (i + curAck) % SEQ_SIZE;
		ack[index] = TRUE;
	}
	// totalSeq = curAck;
	if (curSeq>=curAck)
		totalSeq = totalSeq - (curSeq - curAck);
	else
		totalSeq = totalSeq - (SEQ_SIZE - curAck + curSeq);
	curSeq = curAck;
}


void ackHandler(char c) {
	unsigned char index = (unsigned char)c - 1; //序列号减一
	printf("Recv a ack of %d\n", index + 1);
	if (curAck <= index) {
		for (int i = curAck; i <= index; ++i) {
			ack[i] = TRUE;
		}
		curAck = (index + 1) % SEQ_SIZE;
	}
	else if (curAck != index + 1) {
		//ack 超过了最大值，回到了 curAck 的左边
		for (int i = curAck; i < SEQ_SIZE; ++i) {
			ack[i] = TRUE;
		}
		for (int i = 0; i <= index; ++i) {
			ack[i] = TRUE;
		}
		curAck = index + 1;
	}
}


void printTips()
{
	printf("************************************************\n");
	printf("|     -time to get current time                |\n");
	printf("|     -quit to exit client                     |\n");
	printf("|     -testgbn [X] [Y] to test the gbn         |\n");
	printf("************************************************\n");
}

BOOL lossInLossRatio(float lossRatio) {
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

struct ProxyParam {
};

//主函数
int main(int argc, char* argv[])
{
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
	char data[1024 * 113];
	ZeroMemory(data, sizeof(data));
	icin.read(data, 1024 * 113);
	icin.close();
	totalPacket = ceil(strlen(data) / 1024.0);
	printf("File size is %dB, each packet is 1024B and packet total num is % d\n", strlen(data), totalPacket);
	int recvSize;
	memset(ack, true, sizeof(ack));
	//创建子线程负责传输分组
	ProxyParam* lpProxyParam = new ProxyParam;
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
	while (true) {
		bool sendFlag = true;
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
		else if (strcmp(buffer, "-testgbn") == 0) {
			//进入 gbn 测试阶段
			//首先 server（server 处于 0 状态）向 client 发送 205 状态码（server进入 1 状态）
			//server 等待 client 回复 200 状态码，如果收到（server 进入 2 状态），则开始传输文件，否则延时等待直至超时\
			//在文件传输阶段，server 发送窗口大小设为
			ZeroMemory(buffer, sizeof(buffer));
			int recvSize;
			int waitCount = 0;
			printf("Bgain to test GBN protocol,please don't abort the process\n");
			//加入了一个握手阶段
			//首先服务器向客户端发送一个 205 大小的状态码表示服务器准备好了，可以发送数据
			//客户端收到 205 之后回复一个 200 大小的状态码，表示客户端准备好了，可以接收数据了
			//服务器收到 200 状态码之后，就开始使用 GBN 发送数据了
			printf("Shake hands stage\n");
			int stage = 0;
			bool runFlag = true;
			while (runFlag) {
				switch (stage) {
				case 0://发送 205 阶段
					printf("this is stage 0\n");
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
						if (waitCount > 20) {
							runFlag = false;
							printf("Timeout error\n");
							break;
						}
						Sleep(500);
						continue;
					}
					else {
						if ((unsigned char)buffer[0] == 200) {
							printf("Begin a file transfer\n");
							printf("File size is %dB, each packet is 1024B and packet total num is % d\n", strlen(data), totalPacket);
							curSeq = 0;
							curAck = 0;
							totalSeq = 0;
							waitCount = 0;
							stage = 2;
						}
					}
					break;
				case 2://数据传输阶段
					if (seqIsAvailable() && totalSeq < totalPacket) {
						buffer[0] = curSeq + 1;
						ack[curSeq] = FALSE;
						memcpy(&buffer[1], data + 1024 * totalSeq, 1024);
						printf("send a packet with a seq of %d\n", curSeq + 1);
						sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						++curSeq;
						curSeq %= SEQ_SIZE;
						++totalSeq;
						Sleep(500);
					}
					//等待 Ack，若没有收到，则返回值为-1，计数器+1
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
					if (recvSize < 0) {
						waitCount++;
						//10 次等待 ack 则超时重传
						if (waitCount > 10)
						{
							waitCount = 0;
							timeoutHandler();
						}
					}
					else {
						//收到 ack
						ackHandler(buffer[0]);
						waitCount = 0;
					}
					Sleep(500);
					break;
				}
				if (totalSeq == totalPacket) {
					bool finishFlag = true;
					for (int i = 0; i < SEQ_SIZE; ++i) {
						if (!ack[i]) {
							finishFlag = false;
							break;
						}
					}
					if (finishFlag) {
						printf("File transfer complete\n");
						buffer[0] = 204;
						buffer[1] = '\0';
						sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						// break;
						totalSeq = 0;
						runFlag = false;
						sendFlag = false;
					}
					// printf("File transfer complete\n");
				}
			}
		}
		if (sendFlag)
			sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
		Sleep(500);
	}
	//关闭套接字，卸载库
	CloseHandle(hThread);
	closesocket(sockServer);
	WSACleanup();
	return 0;
}


unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
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
	SOCKADDR_IN recvClient;
	recvClient.sin_addr.S_un.S_addr = inet_addr(CLIENT_IP);
	recvClient.sin_family = AF_INET;
	recvClient.sin_port = htons(CLIENT_PORT);
	//接收缓冲区
	char buffer[BUFFER_LENGTH];
	ZeroMemory(buffer, sizeof(buffer));
	int len = sizeof(SOCKADDR);

	printTips();
	int ret;//受到数据大小
	int interval = 1;//收到数据包之后返回 ack 的间隔，默认为 1 表示每个都返回 ack，0 或者负数均表示所有的都不返回 ack
	char cmd[128];
	float packetLossRatio = 0.1f; //默认包丢失率 0.1
	float ackLossRatio = 0.1f; //默认 ACK 丢失率 0.1
	//用时间作为随机种子，放在循环的最外面
	srand((unsigned)time(NULL));
	std::ofstream out;
	while (true) {
		bool sendFlag = false;
		gets_s(buffer, sizeof(buffer));
		ret = sscanf_s(buffer, "%s%f%f", &cmd, sizeof(cmd), &packetLossRatio, &ackLossRatio);
		//开始 GBN 测试，使用 GBN 协议实现 UDP 可靠文件传输
		if (!strcmp(cmd, "-testgbn")) {
			out.open("client_input.txt");
			printf("%s\n", "Begin to test GBN protocol, please don't abort the process");
			printf("The loss ratio of packet is %.2f,the loss ratio of ack is % .2f\n", packetLossRatio, ackLossRatio);
			int waitCount = 0;
			int stage = 0;
			BOOL b;
			unsigned short seq;//包的序列号
			unsigned short recvSeq;//接收窗口大小为 1，已确认的序列号
			unsigned short waitSeq;//等待的序列号
			sendto(socketClient, "-testgbn", strlen("-testgbn") + 1, 0, (SOCKADDR*)&recvClient, sizeof(SOCKADDR));
			while (true)
			{
				memset(buffer, 0, sizeof(buffer));
				//等待 server 回复设置 UDP 为阻塞模式
				recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&recvClient, &len);
				if ((unsigned char)buffer[0] == 204) {
					printf("\nReceive finished\n");
					sendFlag = true;
					out.close();
					break;
				}
				switch (stage) {
				case 0://等待握手阶段
					if ((unsigned char)buffer[0] == 205)
					{
						printf("Ready for file transmission\n");
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(socketClient, buffer, 2, 0,
							(SOCKADDR*)&recvClient, sizeof(SOCKADDR));
						stage = 1;
						recvSeq = 0;
						waitSeq = 1;
					}
					break;
				case 1://等待接收数据阶段
					seq = (unsigned short)buffer[0];
					//随机法模拟包是否丢失
					b = lossInLossRatio(packetLossRatio);
					if (!b) printf("\nThe packet wished to receive is %d\n", waitSeq);	// modify*3
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;
					}
					printf("recv a packet with a seq of %d\n", seq);
					//如果是期待的包，正确接收，正常确认即可
					if (!(waitSeq - seq)) {
						++waitSeq;
						if (waitSeq == 21) {
							waitSeq = 1;
						}
						//输出数据
						// printf("buffer=%s\n", &buffer[1]);
						buffer[0] = seq;
						recvSeq = seq;
						if (out.is_open()) {
							try {
								out.write(&buffer[1], strlen(&buffer[1]));
							}
							catch (const std::ios_base::failure& e) {
								std::cerr << "Caught an I/O exception: " << e.what()
									<< ", error code: " << e.code() << std::endl;
							}
						}
						else {
							std::cerr << "File not opened!" << std::endl;
						}
					}
					else {
						//如果当前一个包都没有收到，则等待 Seq 为 1 的数据包，不是则不返回 ACK（因为并没有上一个正确的 ACK）
						if (!recvSeq) {
							continue;
						}
						buffer[0] = recvSeq;
						buffer[1] = '\0';
					}
					b = lossInLossRatio(ackLossRatio);
					if (b) {
						printf("The ack of %d loss\n", (unsigned char)buffer[0]);	// modify
						continue;
					}
					sendto(socketClient, buffer, 2, 0, (SOCKADDR*)&recvClient, sizeof(SOCKADDR));
					printf("send a ack of %d\n", (unsigned char)buffer[0]);
					break;
				}
				Sleep(500);
			}
		}
		if (!sendFlag) {
			sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&recvClient, sizeof(SOCKADDR));
			ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&recvClient, &len);
			printf("%s\n\n", buffer);
			if (!strcmp(buffer, "Good bye!")) {
				break;
			}
		}
		printTips();
	}
	//关闭套接字
	closesocket(socketClient);
	WSACleanup();
	return 0;
}