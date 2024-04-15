# 基础知识
1. char和TCHAR：TCHAR在Unicode模式下，是wchar_t\*类型的数组；在ANSI模式下，是char\*类型的数组
2. _tmain是VS的C++程序的入口点
3. 对于GET请求
   1. 200 OK：表示请求已成功，服务器已返回请求的资源。
   2. 304 Not Modified：表示自从上次请求之后，请求的资源未被修改，因此客户端可以继续使用其已有的版本。
   3. 404 Not Found：表示服务器无法找到请求的资源。通常，这意味着资源可能已被删除或不可用，或者URL输入错误。

# 代码解读
1. `InitSocket()`函数
   1. 使用`WSAStartup()`初始化Socket API
   2. 使用`socket()`创建代理套接字，返回描述符至`ProxyServer`，用于接收客户端连接
2. `ProxyThread()`函数
   1. 使用`recv()`接收客户端的数据
   2. 使用自定义函数`ParseHttpHead()`解析HTTP头信息
   3. 使用自定义函数`ConnectToServer()`建立与目标服务器的连接
   4. 使用`send()`将请求转发到目标服务器
   5. 使用`recv()`接收服务器的响应
   6. 使用`send()`将响应返回给客户端
3. `ParseHttpHead()`函数
   1. 解析HTTP请求消息的HTTP头部（ASCII码格式），解析后存入HttpHeader结构体中
   2. ![](./image/http请求消息.jpg)
4. `ConnectToServer()`函数
   1. 解析主机名为IPV4地址，并创建套接字与目标服务器建立连接

# VSCode调教
1. 点击右上角的`运行C/C++文件`
2. 调试配置根据你的文件选择，C文件使用gcc编译，C++文件使用g++编译
3. 在`.vscode/task.json`中的`args`中添加编译命令参数`"-lws2_32"`，以链接到 Windows Socket API 库（Ws2_32.lib）
4. 再次点击`运行C/C++文件`，此时会开启一个终端，执行一个命令，该命令编译C++文件并生成.exe程序，同时运行.exe程序
5. 这个时候`运行C/C++文件`和`调试C/C++文件`就都可以使用了

# 其它问题
1. 所有操作系统都有127.0.0.1这个IP地址，即localhost地址

# 暂存
## 目标服务器返回的数据(未添加缓存功能)
```
HTTP/1.1 200 
Server: Server
Date: Mon, 15 Apr 2024 13:12:14 GMT
Content-Type: text/plain;charset=ISO-8859-1
Content-Length: 1
Connection: keep-alive
Set-Cookie: name=value; HttpOnly
Pragma: no-cache
Expires: Thu, 01 Jan 1970 00:00:00 GMT
Cache-Control: no-cache
Cache-Control: no-store
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS, PUT, DELETE
Access-Control-Allow-Headers: DNT,X-CustomHeader,Keep-Alive,User-Agent,X-Requested-With,If-Modified-Since,Cache-Control,Content-Type

1SSIONID=EE0B84167CABB062F5BD36BEA1BEC902
```
实际就是HTTP响应消息的头部

## 目标服务器返回的数据(添加缓存功能)
已缓存
```
```
未缓存
```
```