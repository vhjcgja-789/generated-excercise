#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "can/can_types.h"
#include "util/result.h"

namespace enc {

// ---- 命令码（指令 FUNC）--------------------------------------------------
enum class Command : uint8_t {
    ReadValue = 0x01,              // 读取编码器值（u32）
    SetId = 0x02,                  // 设置编码器 ID（1~255）
    SetBaud = 0x03,                // 设置 CAN 波特率
    SetMode = 0x04,                // 设置工作模式
    SetAutoTime = 0x05,            // 设置自动回传时间（us，50~65535）
    SetZero = 0x06,                // 当前位置置零点
    SetDirection = 0x07,           // 设置递增方向
    ReadVelocity = 0x0A,           // 读取角速度（i32）
    SetVelocitySampleTime = 0x0B,  // 设置角速度采样时间（ms，0~65535）
    SetMidpoint = 0x0C,            // 当前位置置中点
    SetValue = 0x0D,               // 设置当前位置值（u32）
    SetFiveTurns = 0x0F,           // 当前位置置 5 圈值
};

// ---- 工作模式（0x04 参数）-------------------------------------------------
enum class EncoderMode : uint8_t {
    Query = 0x00,              // 查询模式（出厂默认，演示使用）
    AutoValueStd = 0xAA,       // 标准帧自动回传编码器值
    AutoVelSignedStd = 0x02,   // 标准帧自动回传有符号角速度
    AutoVelUnsignedStd = 0x07, // 标准帧自动回传无符号角速度
    AutoValueExt = 0x18,       // 扩展帧自动回传编码器值
    AutoVelSignedExt = 0x12,   // 扩展帧自动回传有符号角速度
    AutoVelUnsignedExt = 0x17  // 扩展帧自动回传无符号角速度
};

// ---- 波特率（0x03 参数）---------------------------------------------------
enum class BaudRate : uint8_t {
    K500 = 0,                  
    M1 = 1,                    
    K250 = 2,
    K125 = 3,
    K100 = 4
};

// ---- 递增方向（0x07 参数）-------------------------------------------------
enum class Direction : uint8_t {
    CW = 0,                    // 顺时针
    CCW = 1                    // 逆时针
};

// ---- 编码器地址 ------------------------------------------------------------
struct EncoderAddress {
    uint32_t can_id = 1;         // CAN ID
    uint8_t protocol_id = 1;     // 数据域 data[1]
    bool is_extended = false;    

    static EncoderAddress standard(uint8_t id) {
        return EncoderAddress{id, id, false};
    }
};

// ---- 配置 ------------------------------------------------------------------
struct EncoderConfig {
    EncoderAddress address = EncoderAddress::standard(1);
    std::chrono::milliseconds timeout{1000};
};

// ---- 错误 ------------------------------------------------------------------
enum class EncoderErrorCode {
    InvalidArgument,     // 参数超范围，发送前失败
    Transport,           // CAN 层发送/接收失败（transport 携带 CanError）
    Timeout,             // 超时未收到合法应答
    BadResponse,         // 应答帧校验不过（短帧/LEN/ID/FUNC/帧类型）
    EncoderRejected      // 状态响应非 0，encoder_status 为设备错误码
};

struct EncoderError {
    EncoderErrorCode code = EncoderErrorCode::Timeout;
    uint8_t encoder_status = 0;                // EncoderRejected 时有效
    std::optional<can::CanError> transport;    // Transport 时有效
    std::string message;
};


enum class ResponseKind {
    Status8,     // data[3] 是状态码：0=成功，非 0=EncoderRejected
    UInt32Le,    // data[3..6] 整体是业务值，低字节非零也属成功
    Int32Le      // 先组装 u32，再按二进制位模式转为 i32
};

enum class AddressPolicy {
    Current,     // 按当前地址应答（绝大多数命令）
    NewId        // 按新地址应答（仅 0x02 setId，成功后从机用新地址应答）
};

struct CommandSpec {
    Command command;
    const char* name;
    uint8_t request_payload_size;
    uint8_t response_dlc;
    ResponseKind response_kind;
    AddressPolicy address_policy;
};


inline constexpr CommandSpec kCommandTable[] = {
    {Command::ReadValue,              "ReadValue",             1, 7, ResponseKind::UInt32Le, AddressPolicy::Current},
    {Command::SetId,                  "SetId",                 1, 4, ResponseKind::Status8,   AddressPolicy::NewId},
    {Command::SetBaud,                "SetBaudRate",           1, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetMode,                "SetMode",               1, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetAutoTime,            "SetAutoReportTime",     2, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetZero,                "SetZero",               1, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetDirection,           "SetDirection",          1, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::ReadVelocity,           "ReadVelocity",          1, 7, ResponseKind::Int32Le,   AddressPolicy::Current},
    {Command::SetVelocitySampleTime,  "SetVelocitySampleTime", 2, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetMidpoint,            "SetMidpoint",           1, 4, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetValue,               "SetValue",              4, 7, ResponseKind::Status8,   AddressPolicy::Current},
    {Command::SetFiveTurns,           "SetFiveTurns",          1, 4, ResponseKind::Status8,   AddressPolicy::Current},
};

template <typename T>
using EncResult = util::Result<T, EncoderError>;

} // namespace enc
