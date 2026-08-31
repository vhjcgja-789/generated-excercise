# CanSendRecvDemo — USBCAN-II 双通道收发测试

自写 Demo：**通道 0 发送、通道 1 接收**的最小示例。
- 单线程循环：每 10ms 发送一帧 CAN 报文（ID=0x123，标准帧，8 字节，`data[0]` 递增），同一循环里轮询接收并打印 `RX ID=... [长度] 数据`，按任意键退出。
- 二次开发：根据需要修改 `main.cpp`，添加其他功能，如发送自定义 CAN 报文、接收自定义 CAN 报文等。
- 接口函数库：`zlgcan_x64.lib`（x64），`zlgcan.lib`（x86）。
- 接口函数头文件：`zlgcan.h`。

> 工程默认按 **Win32 (x86)** 配置。若编译 x64，需把 `zlgcan.lib` 换成
> `zlgcan(20260414)\zlgcan_x64\zlgcan.lib`，并执行 `copy_runtime.bat x64 [Debug|Release]`。


## VSCode (MinGW) 编译运行

未装 Visual Studio，但有 **MinGW-w64 g++ 8.1.0（x64）**，工程已配好 `.vscode`：

- `tasks.json` —— 编译任务 `build (g++ x64)`：g++ 编译 `main.cpp` 并链接 `zlgcan_x64.lib`，输出 `bin\CanSendRecvDemo.exe`
```bash
# 打开项目目录
cd CanSendRecvDemo

# 编译
g++ -o bin/CanSendRecvDemo.exe main.cpp -lgcan_x64 -Lzlgcan(20260414)\zlgcan_x64

# 运行
.\bin\CanSendRecvDemo.exe
```

