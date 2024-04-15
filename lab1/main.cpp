/*引入头文件和库*/
// #include "stdafx.h" // 需要注释
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#pragma comment(lib, "Ws2_32.lib")
#define MAXSIZE 65507 // 发送数据报文的最大长度
#define HTTP_PORT 80  // http 服务器端口

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

/*代理服务器全局变量*/
SOCKET ProxyServer; // 代理套接字描述符：用于接收来自客户端的连接
sockaddr_in ProxyServerAddr;    // 代理端点地址
const int ProxyPort = 10240;    // 代理端口
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
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    DWORD dwThreadID;
    // 代理服务器不断监听
    while (true)
    {
        acceptSocket = accept(ProxyServer, NULL, NULL);
        lpProxyParam = new ProxyParam;
        if (lpProxyParam == NULL)
        {
            continue;
        }
        lpProxyParam->clientSocket = acceptSocket;
        hThread = (HANDLE)_beginthreadex(NULL, 0,&ProxyThread, (LPVOID)lpProxyParam, 0, 0);
        CloseHandle(hThread);
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
    
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);  // 创建套接字，返回描述符。使用TCP协议（用于监听来自客户端的连接）
    
    if (INVALID_SOCKET == ProxyServer)
    {
        printf("创建套接字失败，错误代码为： %d\n", WSAGetLastError());
        return FALSE;
    }
    
    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort);
    ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    
    if (bind(ProxyServer, (SOCKADDR *)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)  // 绑定套接字的本地端点地址，IP为所有可用IP，端口为ProxyPort
    {
        printf("绑定套接字失败\n");
        return FALSE;
    }
    
    if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR)
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

    // 接受客户端发送的数据
    recvSize = recv(((ProxyParam*)lpParameter)->clientSocket,Buffer, MAXSIZE, 0);
    HttpHeader *httpHeader = new HttpHeader(); // 移动至if之前，否则会报错
    
    // 接受失败或连接被关闭则报错
    if (recvSize <= 0)
    {
        goto error;
    }  

    // 动态分配缓存空间，并复制接受到的数据
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);
    
    // 解析HTTP头部信息
    ParseHttpHead(CacheBuffer, httpHeader);
    delete CacheBuffer;

    // 将请求转发到目标服务器
    if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket,httpHeader->host))
    {
        goto error;
    }
    printf("代理连接主机 %s 成功\n", httpHeader->host);
    // 将客户端发送的 HTTP 数据报文直接转发给目标服务器
    ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
    // 等待目标服务器返回数据
    recvSize = recv(((ProxyParam*)lpParameter)->serverSocket,Buffer, MAXSIZE, 0);
    if (recvSize <= 0)
    {
        goto error;
    }
    // 将目标服务器返回的数据直接转发给客户端
    ret = send(((ProxyParam*)lpParameter)->clientSocket,Buffer, sizeof(Buffer), 0);

// 错误处理
error:
    printf("关闭套接字\n");
    Sleep(200);
    closesocket(((ProxyParam *)lpParameter)->clientSocket);
    closesocket(((ProxyParam *)lpParameter)->serverSocket);
    delete lpParameter;
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
    printf("%s\n", httpHeader->url);
    
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
    HOSTENT *hostent = gethostbyname(host);
    if (!hostent)
    {
        return FALSE;
    }
    in_addr Inaddr = *((in_addr *)*hostent->h_addr_list);
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    *serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (*serverSocket == INVALID_SOCKET)
    {
        return FALSE;
    }
    if (connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}