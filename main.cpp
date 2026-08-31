//=============================================================================
// main.cpp  ——  USBCAN-II 编码器控制（布瑞特多圈 CAN 编码器）
// 硬件连接：编码器 CAN_H/CAN_L 接 USBCAN-II 通道1（CAN1），总线需 120Ω 终端电阻
// 接口调用顺序：OpenDevice -> Init_Channel(通道1, 500kbps)
//               -> 设置ID(0x02) -> 设置零点(0x06)
//               -> 循环(读编码器值 0x01 -> 打印) -> ResetCAN -> CloseDevice
//
// 协议（编码器 CAN 协议指南 V2.01，标准数据帧）：
//   标识符 ID = 编码器节点地址（默认 1）
//   数据域   = [LEN][设备ID][指令FUNC][数据DATA(低字节在前)]，LEN=2+DATA字节数
//   0x02 设置ID    : 发 [0x04][id][0x02][新ID]       -> 收 [0x04][id][0x02][状态]
//   0x06 设零点    : 发 [0x04][id][0x06][0x00]       -> 收 [0x04][id][0x06][状态]
//   0x01 读编码器值: 发 [0x04][id][0x01][0x00]       -> 收 [0x07][id][0x01][v0][v1][v2][v3]
//   状态 0 = 成功，非 0 = 错误码
//
// 运行库：zlgcan.dll + kerneldlls 与 exe 同目录；工程链接 zlgcan_x64.lib
//=============================================================================
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>

#include "zlgcan.h"

//-----------------------------------------------------------------------------
// 编码器配置（如需修改编码器 ID / 波特率，只改这里即可）
//-----------------------------------------------------------------------------
#define ENC_ID    1                // 编码器节点地址（标识符 ID），默认 1，范围 1~255
#define ENC_BAUD  "500000"         // CAN 波特率（编码器出厂默认 500kbps）

//-----------------------------------------------------------------------------
// 构造一帧编码器指令：
//   ID = ENC_ID，数据域 = [LEN][ENC_ID][func][payload...]，LEN = 2 + plen
//   payload 多字节时低字节在前
//-----------------------------------------------------------------------------
void Build_Encoder_Frame(ZCAN_Transmit_Data& tx, BYTE func, const BYTE* payload, int plen)
{
    memset(&tx, 0, sizeof(tx));
    tx.frame.can_id  = MAKE_CAN_ID(ENC_ID, 0, 0, 0);  // 标准数据帧：非扩展、非远程
    tx.frame.can_dlc = (BYTE)(2 + plen);               // DLC = LEN 字节数
    tx.transmit_type = 0;                              // 0 = 正常发送

    tx.frame.data[0] = (BYTE)(2 + plen);               // LEN：含自身、设备ID、FUNC、DATA
    tx.frame.data[1] = ENC_ID;                         // 设备 ID（编码器地址）
    tx.frame.data[2] = func;                           // 指令 FUNC
    for (int i = 0; i < plen && i < 4; ++i) {
        tx.frame.data[3 + i] = payload[i];             // 数据 DATA（低字节在前）
    }
}

//-----------------------------------------------------------------------------
// 初始化通道：设波特率 -> InitCAN（正常模式，全收）-> StartCAN
//-----------------------------------------------------------------------------
CHANNEL_HANDLE Init_Channel(DEVICE_HANDLE dev, int chn_idx)
{
    char path[24] = { 0 };
    snprintf(path, sizeof(path), "%d/baud_rate", chn_idx);
    if (ZCAN_SetValue(dev, path, ENC_BAUD) != STATUS_OK) {
        printf("设置通道%d波特率失败\n", chn_idx);
    }

    ZCAN_CHANNEL_INIT_CONFIG cfg = { 0 };
    cfg.can_type     = TYPE_CAN;      // 经典 CAN
    cfg.can.mode     = 0;             // 正常模式
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
// 接收并原样打印所有收到的帧（便于观察总线上的全部报文）
//-----------------------------------------------------------------------------
void Receive_And_Print(CHANNEL_HANDLE chn)
{
    if (ZCAN_GetReceiveNum(chn, TYPE_CAN) == 0) {
        return;
    }

    ZCAN_Receive_Data rx[10] = { 0 };
    UINT n = ZCAN_Receive(chn, rx, 10, 1000);
    for (UINT i = 0; i < n; i++) {
        printf("RX ID=0x%X [%d] ", GET_ID(rx[i].frame.can_id), rx[i].frame.can_dlc);
        for (int j = 0; j < rx[i].frame.can_dlc; j++) {
            printf("%02X ", rx[i].frame.data[j]);
        }
        printf("\n");
    }
}

//-----------------------------------------------------------------------------
// 发送一帧编码器指令，并在 timeout 毫秒内等待对应应答
// （应答判据：data[1]==ENC_ID 且 data[2]==func）
// 返回：应答状态字节 data[3]（0=成功，非0=错误码）；超时返回 -1
//-----------------------------------------------------------------------------
int Send_And_Wait_Resp(CHANNEL_HANDLE chn, BYTE func, const BYTE* payload, int plen, DWORD timeout)
{
    ZCAN_Transmit_Data tx;
    Build_Encoder_Frame(tx, func, payload, plen);
    if (ZCAN_Transmit(chn, &tx, 1) != 1) {
        printf("发送指令 0x%02X 失败\n", func);
        return -1;
    }
    printf("TX ID=0x%X [%d] ", GET_ID(tx.frame.can_id), tx.frame.can_dlc);
    for (int j = 0; j < tx.frame.can_dlc; j++) {
        printf("%02X ", tx.frame.data[j]);
    }
    printf("\n");

    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < timeout) {
        if (ZCAN_GetReceiveNum(chn, TYPE_CAN) > 0) {
            ZCAN_Receive_Data rx[1] = { 0 };
            UINT n = ZCAN_Receive(chn, rx, 1, 200);
            for (UINT i = 0; i < n; i++) {
                printf("RX ID=0x%X [%d] ", GET_ID(rx[i].frame.can_id), rx[i].frame.can_dlc);
                for (int j = 0; j < rx[i].frame.can_dlc; j++) {
                    printf("%02X ", rx[i].frame.data[j]);
                }
                printf("\n");
                // 匹配本指令的应答：设备 ID 与 FUNC 一致
                if (rx[i].frame.can_dlc >= 4 &&
                    rx[i].frame.data[1] == ENC_ID && rx[i].frame.data[2] == func) {
                    return rx[i].frame.data[3];        // 应答状态：0=成功
                }
            }
        }
        Sleep(5);
    }
    printf("指令 0x%02X 无应答（超时）。检查接线/120Ω终端电阻/波特率/编码器实际ID\n", func);
    return -1;
}

//-----------------------------------------------------------------------------
// 轮询读取编码器值（指令 0x01），应答帧 data[3..6] 为 32 位值（低字节在前）
// 返回 0=读取成功，-1=失败
//-----------------------------------------------------------------------------
int Read_Encoder_Value(CHANNEL_HANDLE chn, DWORD timeout)
{
    BYTE dummy = 0;
    ZCAN_Transmit_Data tx;
    Build_Encoder_Frame(tx, 0x01, &dummy, 1);
    if (ZCAN_Transmit(chn, &tx, 1) != 1) {
        printf("发送读取指令失败\n");
        return -1;
    }

    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < timeout) {
        if (ZCAN_GetReceiveNum(chn, TYPE_CAN) > 0) {
            ZCAN_Receive_Data rx[1] = { 0 };
            UINT n = ZCAN_Receive(chn, rx, 1, 200);
            for (UINT i = 0; i < n; i++) {
                if (rx[i].frame.can_dlc >= 7 &&
                    rx[i].frame.data[1] == ENC_ID && rx[i].frame.data[2] == 0x01) {
                    UINT val = (UINT)rx[i].frame.data[3]
                             | ((UINT)rx[i].frame.data[4] << 8)
                             | ((UINT)rx[i].frame.data[5] << 16)
                             | ((UINT)rx[i].frame.data[6] << 24);
                    printf("编码器值 = %u (0x%08X)\n", val, val);
                    return 0;
                }
                // 非本指令应答也原样打印，便于观察
                printf("RX ID=0x%X [%d] ", GET_ID(rx[i].frame.can_id), rx[i].frame.can_dlc);
                for (int j = 0; j < rx[i].frame.can_dlc; j++) {
                    printf("%02X ", rx[i].frame.data[j]);
                }
                printf("\n");
            }
        }
        Sleep(5);
    }
    printf("读取编码器值超时\n");
    return -1;
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

    // 只初始化通道1：编码器接在通道1，同一通道收发
    CHANNEL_HANDLE chn = Init_Channel(dev, 1);
    if (chn == INVALID_CHANNEL_HANDLE) {
        ZCAN_CloseDevice(dev);
        return 1;
    }

    printf("=== 布瑞特多圈编码器控制 ===\n");
    printf("通道1 @ %s bps，编码器 ID = %d\n\n", ENC_BAUD, ENC_ID);

    // 1) 配置编码器 ID（保持/复位为 ENC_ID，指令 0x02）
    BYTE id = (BYTE)ENC_ID;
    int st = Send_And_Wait_Resp(chn, 0x02, &id, 1, 1000);
    printf("设置编码器 ID = %d: %s\n\n", ENC_ID,
           st == 0 ? "成功" : (st < 0 ? "无应答" : "失败"));

    // 2) 设置当前位置为零点（指令 0x06）
    //    注意：请在启动本程序前，先把编码器轴转到目标零点位置！
    printf("【提示】请确认编码器轴已处于目标零点位置，即将把当前位置设为 0\n");
    BYTE zero = 0;
    st = Send_And_Wait_Resp(chn, 0x06, &zero, 1, 1000);
    printf("设置零点(当前位置置0): %s\n\n", st == 0 ? "成功" : (st < 0 ? "无应答" : "失败"));

    // 3) 循环轮询读取编码器值（指令 0x01），按任意键退出
    printf("开始轮询读取编码器值（每 500ms 一次），按任意键退出...\n");
    while (!_kbhit()) {
        Read_Encoder_Value(chn, 500);
        Sleep(500);
    }

    // 收尾
    ZCAN_ResetCAN(chn);
    ZCAN_CloseDevice(dev);
    return 0;
}
