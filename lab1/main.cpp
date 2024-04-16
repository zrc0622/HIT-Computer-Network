/*引入头文件和库*/
// #include "stdafx.h" // 需要注释
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <string>
#include <iostream>
#pragma comment(lib, "Ws2_32.lib")
#define MAXSIZE 65507 // 发送数据报文的最大长度
#define HTTP_PORT 80  // http 服务器端口
#define banedWeb "http://today.hit.edu.cn/"  // 不允许访问的网站（今日哈工大）
#define fishWebSrc "http://www.hit.edu.cn/" // 钓鱼源网站
#define fishWebTarget "https://future.hit.edu.cn/"  // 钓鱼目的网站：哈工大主页->未来技术主页
#define fishWebHost "future.hit.edu.cn" // 钓鱼目的网站的主机名
#define banedIP "127.0.0.1" // 不允许接入的用户IP（本机）

/*HTTP头部结构定义*/
struct HttpHeader
{
    char method[4];         // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
    char url[1024];         // 请求的 url
    char host[1024];        // 目标主机
    char cookie[1024 * 10]; // cookie
    HttpHeader()
    {
        ZeroMemory(this, sizeof(HttpHeader));   // HttpHeader()为结构体的构造函数，实例化结构体后其成员变量初始化为0
    }
};

/*函数声明*/
BOOL InitSocket();
void ParseHttpHead(char *buffer, HttpHeader *httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
void makeCachename(char* url, char* filename);
void getCachedate(FILE* in, char* date);
void addDate(char* buffer, char* date);
boolean useCache(char* buffer, char* cachename);
void makeCache(char* buffer, char* url);

/*代理服务器全局变量*/
SOCKET ProxyServer; // 代理套接字描述符：用于接收来自客户端的连接
sockaddr_in ProxyServerAddr;    // 代理端点地址
const int ProxyPort = 8080;    // 代理端口
std::string cacheDir = "./cache/";  // cache文件夹路径
int ifBanUser = false;
// 由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
// 可以使用线程池技术提高服务器效率
// const int ProxyThreadMaxNum = 20;
// HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
// DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam
{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

/*主函数*/
int main(int argc, char *argv[]) //  _tmain改为main，_TCHAR改为char
{
    printf("代理服务器正在启动\n");
    printf("初始化...\n");
    if (!InitSocket()) 
    {
        printf("socket 初始化失败\n");
        return -1;
    }
    printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
    printf("是否禁止用户 %s 访问外部网站，输入1禁止，输入0允许：", banedIP);
    scanf("%d", &ifBanUser);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    DWORD dwThreadID;
    sockaddr_in acceptAdd;  // 用于保存客户端信息
    int addLen = sizeof(acceptAdd);
    // 代理服务器不断监听
    while (true)
    {
        acceptSocket = accept(ProxyServer, (SOCKADDR*)&acceptAdd, &addLen); // 4. 阻塞函数accept对每个到来的连接请求建立连接
        lpProxyParam = new ProxyParam;
        if (lpProxyParam == NULL)
        {
            continue;
        }
        // 实现不允许部分用户连接
        if(strcmp(inet_ntoa(acceptAdd.sin_addr), banedIP)==0 && ifBanUser==1)
        {
            printf("用户 %s 禁止访问\n", banedIP);
            continue;
        }
        printf("用户 %s 已连接\n", inet_ntoa(acceptAdd.sin_addr));
        lpProxyParam->clientSocket = acceptSocket;
        hThread = (HANDLE)_beginthreadex(NULL, 0,&ProxyThread, (LPVOID)lpProxyParam, 0, 0); // 创建新线程，执行ProxyThread函数
        CloseHandle(hThread);   // 关闭线程句柄，但子线程仍在运行
        Sleep(200);
    }
    closesocket(ProxyServer);
    WSACleanup();
    return 0;
}

/*函数实现*/
//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket()
{
    // 加载套接字库（必须）
    WORD wVersionRequested;
    WSADATA wsaData;
    // 套接字加载时错误提示
    int err;
    // 版本 2.2
    wVersionRequested = MAKEWORD(2, 2); // 使用Winsock2.2
    // 加载 dll 文件 Scoket 库
    err = WSAStartup(wVersionRequested, &wsaData); // 初始化Socket API（Winsock）
    
    if (err != 0)
    {
        // 找不到 winsock.dll
        printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
        return FALSE;
    }
    
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        printf("不能找到正确的 winsock 版本\n");
        WSACleanup();
        return FALSE;
    }
    
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);  // 1. 创建套接字，返回描述符。使用TCP协议（用于监听来自客户端的连接请求）
    
    if (INVALID_SOCKET == ProxyServer)
    {
        printf("创建套接字失败，错误代码为： %d\n", WSAGetLastError());
        return FALSE;
    }
    
    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort);
    ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    
    if (bind(ProxyServer, (SOCKADDR *)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)  // 2. 绑定套接字的本地端点地址，IP为所有可用IP，端口为ProxyPort
    {
        printf("绑定套接字失败\n");
        return FALSE;
    }
    
    if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) // 3. 套接字进入监听模式，等待客户端发送连接请求
    {
        printf("监听端口%d 失败", ProxyPort);
        return FALSE;
    }
    return TRUE;
}

//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter)
{
    char Buffer[MAXSIZE];
    char *CacheBuffer;
    ZeroMemory(Buffer, MAXSIZE);
    SOCKADDR_IN clientAddr;
    int length = sizeof(SOCKADDR_IN);
    int recvSize;
    int ret;

    boolean ifHave = false;  // 本地是否有缓存文件
    boolean ifMake = true;   // 是否需要新建或更新缓存文件
    FILE* fileIn;   // 文件输入指针
    char cachename[105];
    char date[50];

    // 接受客户端发送的数据
    recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0); // 5. 从客户端接收数据
    HttpHeader *httpHeader = new HttpHeader(); // 移动至if之前，否则会报错
    
    // 接受失败或连接被关闭则报错
    if (recvSize <= 0)
    {
        goto error;
    }  

    // 动态分配缓存空间，并复制接受到的数据
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);  // 将数据从Buffer复制到CacheBuffer中
    
    // 解析HTTP头部信息
    ParseHttpHead(CacheBuffer, httpHeader);
    // 确认是否存在缓存
    makeCachename(httpHeader->url, cachename); // http://jwts.hit.edu.cn/queryDlfs -> httpjwtshiteducnqueryDlfs.txt
    if(fopen_s(&fileIn, (cacheDir+cachename).c_str(), "r")==0)
    {
        printf("存在缓存\n");
        getCachedate(fileIn, date); // 获取缓存日期
        fclose(fileIn);
        addDate(Buffer, date); // 在HTTP请求头前添加缓存日期
        ifHave = true;
    }
    else printf("需要新建本地缓存\n"); 
    
    // 网站过滤
    if(strcmp(httpHeader->url, banedWeb) == 0)
    {
        printf("禁止访问：%s\n", banedWeb);
        goto error;
    }
    delete CacheBuffer;

    // 将请求转发到目标服务器
    if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket,httpHeader->host)) // 6. 创建与目标服务器连接的套接字    7. 与目标服务器连接
    {
        goto error;
    }
    printf("代理连接主机 %s 成功\n", httpHeader->host);
    // 将客户端发送的 HTTP 数据报文直接转发给目标服务器
    ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);   // 8. 向目标服务器发送报文
    // 等待目标服务器返回数据
    recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0); // 9. 接收目标服务器返回数据
    if (recvSize <= 0)
    {
        goto error;
    }
    if(ifHave) ifMake = useCache(Buffer, cachename); // 内部判断是否使用缓存文件
    if(ifMake) makeCache(Buffer, httpHeader->url); // 新建或更新缓存文件
    
    // 将目标服务器返回的数据直接转发给客户端
    ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0); // 10. 将目标服务器数据发送回客户端
// 错误处理
error:
    printf("关闭套接字\n");
    printf("************************************************\n");
    Sleep(200);
    closesocket(((ProxyParam *)lpParameter)->clientSocket);
    closesocket(((ProxyParam *)lpParameter)->serverSocket);
    // delete lpParameter;
    delete static_cast<ProxyParam*>(lpParameter);   // 解决删除空指针warning
    _endthreadex(0);
    return 0;
}

//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer, HttpHeader *httpHeader)
{
    char *p;
    char *ptr;
    const char *delim = "\r\n"; // 分隔符：HTTP头部每行的结束符
    
    p = strtok_s(buffer, delim, &ptr); // 提取第一行
    printf("%s\n", p);
    
    // 请求方法：GET or POST
    if (p[0] == 'G')
    { // GET 方式
        memcpy(httpHeader->method, "GET", 3);
        memcpy(httpHeader->url, &p[4], strlen(p) - 13); // 从请求行中提取URL
    }
    else if (p[0] == 'P')
    { // POST 方式
        memcpy(httpHeader->method, "POST", 4);
        memcpy(httpHeader->url, &p[5], strlen(p) - 14); // 从请求行中提取URL
    }
    // printf("%s\n", httpHeader->url);
    
    // 循环提取后续部分，包括主机名HOST、Cookie
    p = strtok_s(NULL, delim, &ptr);
    while (p)
    {
        switch (p[0])
        {
        case 'H': // Host
            memcpy(httpHeader->host, &p[6], strlen(p) - 6); // 提取主机名
            break;
        case 'C': // Cookie
            if (strlen(p) > 8)
            {
                char header[8];
                ZeroMemory(header, sizeof(header));
                memcpy(header, p, 6);
                if (!strcmp(header, "Cookie"))
                {
                    memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);   // 提取cookie
                }
            }
            break;
        default:
            break;
        }
        p = strtok_s(NULL, delim, &ptr);
    }
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET *serverSocket, char *host)
{
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(HTTP_PORT);
    
    // 将主机名（域名）解析为IP地址
    HOSTENT *hostent = gethostbyname(host);
    if (!hostent)
    {
        return FALSE;
    }

    in_addr Inaddr = *((in_addr *)*hostent->h_addr_list);
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    
    // 创建新的套接字，用于与目标服务器连接
    *serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (*serverSocket == INVALID_SOCKET)
    {
        return FALSE;
    }

    // 连接到目标服务器
    if (connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}

/*以下为缓存功能使用的函数*/
/*构造缓存文件名*/
void makeCachename(char* url, char* cachename)
{
	int count = 0;
	while (*url != '\0') {
		if ((*url >= 'a' && *url <= 'z') || (*url >= 'A' && *url <= 'Z') || (*url >= '0' && *url <= '9')) 
        {
			*cachename++ = *url;
			count++;
		}
		if(count >= 100) break;
		url++;
	}
	strcat(cachename, ".txt");
}

/*获取缓存文件中的日期*/
void getCachedate(FILE* in, char* date)
{
    // 逐行寻找，直到包含 "Date" 字段的行
    char target[5] = "Date";
    char *p, *ptr;
    char buffer[MAXSIZE];
    ZeroMemory(buffer, MAXSIZE);
    fread(buffer, sizeof(char), MAXSIZE, in);
    const char* delim = "\r\n"; //换行符
    p = strtok_s(buffer, delim, &ptr);  // 提取一行
    int len = strlen(target) + 2;   // 只需要“Date: ”后的
    while (p)
    {
        if(strstr(p, target) != NULL)   // 是否有匹配的
        {
            memcpy(date, &p[len], strlen(p) - len);
            return;
        }
        p = strtok_s(NULL, delim, &ptr);
    }
}

/*为HTTP请求报文添加字段*/
void addDate(char* buffer, char* date)
{
    const char* field = "Host";
    const char* newfield = "if-modified-since: ";  // 定义将要插入的新字段 "if-modified-since:"
    //const char *delim = "\r\n";

    char temp[MAXSIZE];  // 临时数组，用于存储原始数据的副本
    ZeroMemory(temp, MAXSIZE);

    char* pos = strstr(buffer, field);  // 找到"Host"字段的位置
    
    for(int i = 0; i < strlen(pos); i++) temp[i] = pos[i];  // 备份
    *pos = '\0';  // 在"Host"的位置插入字符串结束符，截断原始buffer

    while(*newfield != '\0') *pos++ = *newfield++;      // 插入"if-modified-since: "

    while(*date != '\0') *pos++ = *date++;  // 插入日期
    *pos++ = '\r';  // 在日期后添加回车符
    *pos++ = '\n';  // 添加换行符

    for(int i = 0; i < strlen(temp); i++)  *pos++ = temp[i];  // 将temp中的数据复制回buffer
}

/*判断是否使用本地的缓存文件*/
boolean useCache(char* buffer, char* cachename)
{
    char *p, *ptr, tempBuffer[MAXSIZE + 1];
    ZeroMemory(tempBuffer, MAXSIZE + 1);
    const char* delim = "\r\n";  // 分隔符
    memcpy(tempBuffer, buffer, strlen(buffer));
    
    p = strtok_s(tempBuffer, delim, &ptr);  // 找到并返回第一行，通常是状态行

    // 主机返回的报文中的状态码为304时返回已缓存的内容
    if (strstr(p, "304") != NULL)  // 检查是否包含"304"状态码
    { 
        printf("使用本地缓存\n"); 
        ZeroMemory(buffer, strlen(buffer));  // 清空原始buffer
        FILE* in = NULL; 
        if (fopen_s(&in, (cacheDir+cachename).c_str(), "r") == 0) 
        { 
            fread(buffer, sizeof(char), MAXSIZE, in);  // 从缓存中读取数据到buffer
            fclose(in);
        }
        return false;  // 返回false，不需要更新缓存
    }
    printf("需要更新本地缓存\n"); 
    return true;    // 需要更新缓存

}

/*新建或更新缓存文件*/
void makeCache(char* buffer, char* url)
{
    char* p, * ptr, tempBuffer[MAXSIZE + 1];
    ZeroMemory(tempBuffer, MAXSIZE + 1);
    const char* delim = "\r\n";
    memcpy(tempBuffer, buffer, strlen(buffer));

    p = strtok_s(tempBuffer, delim, &ptr);  // 获取HTTP响应的第一行，包含状态码

    if (strstr(tempBuffer, "200") != NULL) // 检查第一行中是否包含状态码"200"
    {
        char cachename[105] = { 0 };
        makeCachename(url, cachename);

        FILE* out;
        fopen_s(&out, (cacheDir+cachename).c_str(), "w+");  // 使用fopen_s打开或创建文件，如果文件存在则清空
        fwrite(buffer, sizeof(char), strlen(buffer), out);  // 将buffer写入文件
        fclose(out);

        printf("新建或更新本地缓存成功，缓存名：%s\n", cachename);  // 打印缓存成功的消息
    }
}