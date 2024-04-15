# 基础知识
1. char和TCHAR：TCHAR在Unicode模式下，是wchar_t\*类型的数组；在ANSI模式下，是char\*类型的数组
2. _tmain是VS的C++程序的入口点

# VSCode调教
1. 点击右上角的`运行C/C++文件`
2. 调试配置根据你的文件选择，C文件使用gcc编译，C++文件使用g++编译
3. 在`.vscode/task.json`中的`args`中添加编译命令参数`"-lws2_32"`，以链接到 Windows Socket API 库（Ws2_32.lib）
4. 再次点击`运行C/C++文件`，此时会开启一个终端，执行一个命令，该命令编译C++文件并生成.exe程序，同时运行.exe程序
5. 这个时候`运行C/C++文件`和`调试C/C++文件`就都可以使用了