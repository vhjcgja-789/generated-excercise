# EncoderDemo — USBCAN-II 与布瑞特多圈编码器通信

## 1. 项目简介

课程设计工程：**USBCAN-II 分析仪（通道 1）与布瑞特多圈 CAN 编码器**通信。

- 控制台程序（`main.cpp`）
- 上位机（`bindings/pyqt/`）


## 2. 项目架构

三层依赖单向：演示入口 → 编码器协议层 → CAN 传输层 → 厂商 SDK。

```text
main.cpp / bindings/pyqt      演示入口（控制台 / 上位机，只做展示与转发）
        │
        ▼
src/encoder/                  协议层 enc::Encoder（只依赖 ICanBus 接口，
        │                      不接触厂商 SDK；组帧/校验/超时/规格表）
        ▼
src/can/                      传输层 can::UsbCanBus（PImpl，
        │                      全工程唯一包含 zlgcan.h 的地方）
        ▼
third_party/zlg/ + bin/zlgcan.dll + bin/kerneldlls/    厂商 SDK
```

```text
main.cpp                      控制台演示入口（唯一允许控制台输出）
src/can/can_types.h           CanFrame / ReceivedFrame / CanError / ICanBus
src/can/usbcan_bus.h/.cpp     USBCAN-II 适配（PImpl；全工程唯一包含 zlgcan.h）
src/encoder/encoder_types.h   地址/命令/模式/配置/错误 + 12 条指令规格表
src/encoder/encoder.h/.cpp    组帧解析、严格帧校验、简单事务
src/util/result.h             Result<T,E>
bindings/c_api.h/.cpp         上位机 C ABI：固定宽度 POD/句柄/错误码，吞异常
bindings/pyqt/                PyQt5 上位机（ctypes 封装 + 设备线程 + 界面）
third_party/zlg/              厂商 SDK 头文件与导入库
bin/                          构建产物与运行库（exe / encoder_core.dll / zlgcan.dll / kerneldlls）
docs/                         参考手册（USBCAN 手册、编码器协议指南）
copy_runtime.bat              复制 SDK 运行库到输出目录
.vscode/                      编译/运行/调试任务
```

## 3. 环境依赖及安装方法

| 依赖 | 版本/说明 |
|------|-----------|
| MinGW-w64 g++ | 8.1.0 x64（posix-seh） |
| Python | 3.13 **x64** | 
| PyQt5 | ≥ 5.15.11 | 


## 4. 编译与运行

### 4.1 控制台程序

```bash
# 构建（输出 bin\EncoderDemo.exe；-static 静态链入运行时，免带 MinGW DLL）
g++ -m64 -std=c++17 -O2 -g -static -I. -Isrc -Ithird_party/zlg main.cpp src/can/usbcan_bus.cpp src/encoder/encoder.cpp third_party/zlg/zlgcan_x64.lib -o bin/EncoderDemo.exe

# 复制 SDK 运行库 zlgcan.dll + kerneldlls 到 bin（只需一次）
.\copy_runtime.bat x64 bin

# 运行
.\bin\EncoderDemo.exe
```

或直接用 VSCode 任务：`build encoder demo`（Ctrl+Shift+B）→ `run encoder demo`。

运行现象：设备打开成功 → 设置 ID=1 成功 → 置零点成功 → 每 500ms 打印
编码器值与角速度 → 按任意键退出。转动编码器轴，值随动、零点处为 0 即正常。

### 4.2 上位机（PyQt5）

```bash
# 构建核心 DLL（输出 bin\encoder_core.dll，与 zlgcan.dll 同目录）
g++ -m64 -std=c++17 -O2 -g -static -shared -DENC_BUILD_DLL -I. -Isrc -Ithird_party/zlg bindings/c_api.cpp src/can/usbcan_bus.cpp src/encoder/encoder.cpp third_party/zlg/zlgcan_x64.lib -o bin/encoder_core.dll

# 冒烟测试（无硬件即可，验证 DLL 加载与错误通道）
python -m bindings.pyqt.smoke_test

# 运行上位机
python -m bindings.pyqt.main
```

