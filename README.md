# CanSendRecvDemo — USBCAN-II 双通道收发测试

自写 Demo：**通道 0 发送、通道 1 接收**的最小示例，用于学习 ZLGCAN 接口调用流程。单线程循环：每 10ms 发送一帧 CAN 报文（ID=0x123，标准帧，8 字节，`data[0]` 递增），同一循环里轮询接收并打印 `RX ID=... [长度] 数据`，按任意键退出。

## 目录结构

```
CanSendRecvDemo/
├── CanSendRecvDemo.sln / .vcxproj / .filters   VS2013(v120) 工程（Win32/x64）
├── main.cpp                                    全部程序逻辑
├── zlgcan.h / canframe.h / config.h / typedef.h 接口头文件（取自 zlgcan(20260414) 最新版）
├── zlgcan.lib                                  导入库（zlgcan_x86）
├── Debug\zlgcan.dll + Debug\kerneldlls\        运行库（x86，已就位）
├── Release\zlgcan.dll + Release\kerneldlls\    运行库（x86，已就位）
├── copy_runtime.bat                            一键复制运行库脚本
└── README.md
```

> 工程默认按 **Win32 (x86)** 配置。若编译 x64，需把 `zlgcan.lib` 换成
> `zlgcan(20260414)\zlgcan_x64\zlgcan.lib`，并执行 `copy_runtime.bat x64 [Debug|Release]`。

## 编译运行

1. 用 **Visual Studio 2013** 打开 `CanSendRecvDemo.sln`（新版 VS 打开时会提示重定目标，选"确定"即可；或手动把工程属性里的平台工具集改为你本机安装的版本）。
2. 平台选择 **Win32**（默认已配置）。
3. `F5` 或 `Ctrl+F5` 编译运行。
4. 若 exe 目录下缺少 `zlgcan.dll` / `kerneldlls`（例如重新 Build 后），双击 `copy_runtime.bat` 重新复制。

## VSCode (MinGW) 编译运行

本机未装 Visual Studio，但有 **MinGW-w64 g++ 8.1.0（x64）**，工程已配好 `.vscode`：

- `tasks.json` —— 编译任务 `build (g++ x64)`：g++ 编译 `main.cpp` 并链接 `zlgcan_x64.lib`，输出 `bin\CanSendRecvDemo.exe`（g++ 可直接链接 MSVC 的 x64 导入库，无需转换）。
- `launch.json` —— 调试配置：先自动编译，再用 gdb 启动 `bin` 下的 exe（工作目录设为 `bin`，保证找到 `zlgcan.dll` 和 `kerneldlls`）。
- `c_cpp_properties.json` —— IntelliSense 头文件路径。

使用步骤：

1. 用 VSCode 打开本目录（`D:\school\CanSendRecvDemo`），安装扩展 **C/C++**（ms-vscode.cpptools，运行调试必需）。
2. 按 `Ctrl+Shift+B`（或 终端 → 运行生成任务）编译。
3. 直接 `F5` 编译并运行（gdb 调试，可打断点单步看接口返回值）；或手动运行 `bin\CanSendRecvDemo.exe`。
4. 运行前接线见下文；`bin\` 内已放好 **x64** 版 `zlgcan.dll` + `kerneldlls`（取自 `zlgcan(20260414)\zlgcan_x64`）。

> 注意：MinGW 是 64 位编译器，所以本项目在 VSCode 下走的是 **x64** 路线（对应 `zlgcan_x64.lib` + x64 运行库）；VS 工程里默认的 Win32/x86 配置与此无关、互不影响。
> 若你的 MinGW 路径不同，请同步修改 `.vscode` 三个文件里的 `D:/webdownload/...` 路径。

## 接线（关键）

USBCAN-II 的两个通道相互独立，**必须外部互联才能对发**：

```
CH0_H  <──>  CH1_H
CH0_L  <──>  CH1_L
```

- 两通道波特率必须一致（程序里均为 500kbps）。
- 两通道各使能一个 120Ω 终端电阻（或总线上已配置等效电阻）。

## 预期现象

| 状态 | 现象 |
|---|---|
| 未接线（或波特率不一致） | 通道 1 无输出（发送无节点应答）——属预期 |
| 接线正确 | 通道 1 持续打印 `RX ID=0x123 [8] 00 01 02 ...`，首字节递增 |

## 排错对照表

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 打开设备失败 | 设备未插 / 驱动未装 | 检查设备管理器 |
| 打开设备失败 | zlgcan.dll / kerneldlls 不在 exe 同目录 | 运行 copy_runtime.bat |
| 打开设备失败 | 设备被 ZCANPRO 等占用 | 关闭 ZCANPRO 后重试 |
| 初始化/启动通道失败 | 设备类型号与硬件不符 | 确认是 USBCAN-II（类型号 4） |
| 收不到帧 | 未接线 / H、L 接反 | 按上文接线，H 对 H、L 对 L |
| 收不到帧 | 两通道波特率不一致 | 统一为 500kbps |
| 收不到帧 | 滤波被误开 | 本程序 acc_mask=0xFFFFFFFF（全收），勿改为滤波 |
| 发送失败多 | 总线上无节点应答 | 双通道互联后即互为节点 |
| 错误帧多 | 终端电阻未接、接触不良 | 检查电阻与接插件；可用 ZCAN_ReadChannelErrInfo 查错误码 |

## 代码要点

- `main.cpp` 刻意精简：无线程、无统计、无防御性检查，只保留接口调用主线，便于对照手册学习。
- 调用顺序：`ZCAN_OpenDevice` → `ZCAN_SetValue("n/baud_rate")` → `ZCAN_InitCAN` → `ZCAN_StartCAN` → 循环 `ZCAN_Transmit` / `ZCAN_GetReceiveNum` + `ZCAN_Receive` → `ZCAN_ResetCAN` → `ZCAN_CloseDevice`。
- 单文件、无预编译头，方便移植到任意 VS 版本。

## 常用修改点

| 想改什么 | 改哪里 |
|---|---|
| 波特率 | `InitChannel()` 中 `"500000"`（两通道都改） |
| 发送帧 ID | `main()` 中 `MAKE_CAN_ID(0x123, 0, 0, 0)` |
| 发送周期 | `main()` 中 `Sleep(10)`（单位 ms） |
| 扩展帧 | `MAKE_CAN_ID(0x123, 1, 0, 0)`（第 2 参数=1） |
| 设备索引 | `ZCAN_OpenDevice(ZCAN_USBCAN2, 0, 0)` 的第 2 参数 |
| 只听模式 | `cfg.can.mode = 1` |
