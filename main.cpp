//=============================================================================
// main.cpp  ——  USBCAN-II 最小示例：通道0发送 -> 通道1接收
// 接口调用顺序：OpenDevice -> Init_Channel(通道0/1)
//               -> 循环(Construct -> Send -> Receive) -> ResetCAN -> CloseDevice
//
// 运行库：zlgcan.dll + kerneldlls 与 exe 同目录；工程链接 zlgcan.lib（x64 用 zlgcan_x64.lib）
//=============================================================================
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>

#include "zlgcan.h"

//-----------------------------------------------------------------------------
// 1.构造帧：组装一条标准数据帧（8 字节，首字节从 seq 开始递增）
//   can_data : 待填充的发送结构体（引用方式传出）
//   id       : 报文 ID（标准帧 11 位）
//   seq      : 序列号，数据首字节 = seq，便于观察收发连续性
//-----------------------------------------------------------------------------
void Construct_CAN_Frame(ZCAN_Transmit_Data& can_data, canid_t id, int seq)
{
    memset(&can_data, 0, sizeof(can_data));
    can_data.frame.can_id  = MAKE_CAN_ID(id, 0, 0, 0);  // 标准数据帧：非扩展、非远程、非错误帧
    can_data.frame.can_dlc = 8;                          // 数据长度
    can_data.transmit_type = 0;                          // 0 = 正常发送

    for (int i = 0; i < 8; ++i) {
        can_data.frame.data[i] = (BYTE)(seq + i);        // 填充数据：seq, seq+1, ...
    }
}

//-----------------------------------------------------------------------------
// 2.初始化通道：设波特率（必须在 InitCAN 之前）-> InitCAN -> StartCAN
//   返回通道句柄；任一环节失败则打印原因并返回 INVALID_CHANNEL_HANDLE
//-----------------------------------------------------------------------------
CHANNEL_HANDLE Init_Channel(DEVICE_HANDLE dev, int chn_idx)
{
    // 设置波特率：属性路径形如 "0/baud_rate"、"1/baud_rate"
    char path[24] = { 0 };
    snprintf(path, sizeof(path), "%d/baud_rate", chn_idx);
    if (ZCAN_SetValue(dev, path, "500000") != STATUS_OK) {
        printf("设置通道%d波特率失败\n", chn_idx);
    }

    // 通道初始化配置：正常模式、全收（acc_mask 全 1 = 不屏蔽任何位）
    ZCAN_CHANNEL_INIT_CONFIG cfg = { 0 };
    cfg.can_type     = TYPE_CAN;      // 0 = 经典 CAN
    cfg.can.mode     = 0;             // 0 = 正常模式，1 = 只听模式
    cfg.can.acc_code = 0;             // 验收码
    cfg.can.acc_mask = 0xffffffff;    // 屏蔽码全 1 = 接收全部报文

    CHANNEL_HANDLE chn = ZCAN_InitCAN(dev, chn_idx, &cfg);
    if (chn == INVALID_CHANNEL_HANDLE) {
        printf("初始化通道%d失败\n", chn_idx);
        return nullptr;
    }

    if (ZCAN_StartCAN(chn) != STATUS_OK) {
        printf("启动通道%d失败\n", chn_idx);
        return nullptr;
    }

    return chn;
}

//-----------------------------------------------------------------------------
// 3.发送：向指定通道发送一帧，返回实际发送成功的帧数（1=成功，0=失败）
//   失败提示只在第一次失败时打印，避免未接线时每 10ms 刷屏
//-----------------------------------------------------------------------------
UINT Send_Frame(CHANNEL_HANDLE chn, ZCAN_Transmit_Data* frame)
{
    UINT n = ZCAN_Transmit(chn, frame, 1);

    static int warned = 0;             
    if (n != 1 && !warned) {
        warned = 1;
        printf("发送失败\n");
    }
    return n;
}

//-----------------------------------------------------------------------------
// 4.接收：先查可读帧数，再批量取走并打印，返回本次收到的帧数
//-----------------------------------------------------------------------------
UINT Receive_And_Print(CHANNEL_HANDLE chn)
{
    // 先查后取：type = TYPE_CAN 表示查询 CAN 帧
    if (ZCAN_GetReceiveNum(chn, TYPE_CAN) == 0) {
        return 0;
    }

    ZCAN_Receive_Data rx[10] = { 0 };
    UINT n = ZCAN_Receive(chn, rx, 10, 1000);    // 最多取 10 帧，等待 1000ms
    for (UINT i = 0; i < n; i++) {
        printf("RX ID=0x%X [%d] ", GET_ID(rx[i].frame.can_id), rx[i].frame.can_dlc);
        for (int j = 0; j < rx[i].frame.can_dlc; j++) {
            printf("%02X ", rx[i].frame.data[j]);
        }
        printf("\n");
    }
    return n;
}


int main(void)
{
    SetConsoleOutputCP(CP_UTF8);

    // 打开设备：USBCAN-II（经典 CAN），设备索引 0
    DEVICE_HANDLE dev = ZCAN_OpenDevice(ZCAN_USBCAN2, 0, 0);
    if (dev == INVALID_DEVICE_HANDLE) {
        printf("打开设备失败\n");
        return 1;
    }

    // 初始化两个通道：通道0 发送，通道1 接收
    CHANNEL_HANDLE chTx = Init_Channel(dev, 0);
    CHANNEL_HANDLE chRx = Init_Channel(dev, 1);
    if (chTx == INVALID_CHANNEL_HANDLE || chRx == INVALID_CHANNEL_HANDLE) {
        ZCAN_CloseDevice(dev);
        return 1;
    }

    // 发循环：每 10ms 构造并发送一帧，同时轮询接收
    printf("通道0 发送 -> 通道1 接收，按任意键停止\n");
    int seq = 0;
    while (!_kbhit()) {
        ZCAN_Transmit_Data tx;
        Construct_CAN_Frame(tx, 0x123, seq);    // 构造帧
        Send_Frame(chTx, &tx);                  // 发送
        Receive_And_Print(chRx);                // 接收并打印

        seq++;
        Sleep(1000);
    }

    // 收尾：复位两个通道，关闭设备
    ZCAN_ResetCAN(chTx);
    ZCAN_ResetCAN(chRx);
    ZCAN_CloseDevice(dev);
    return 0;
}
