# 生成命令
```
cd GBN
g++ -o GBN_server.exe GBN_server.cpp -lws2_32
```

# 编译方式
1. 打开Developer Command Prompt for VS 2019
2. cd 到对应文件夹
3. cl -o GBN_client.cpp

# 其它
1. 运行代码前终端输入`chcp 65001`

# 注意
1. 大部分火炬的GBN，当传输文件需要的序列数大于代码设定的序列数时会发生传输错误，本代码修复了这个bug