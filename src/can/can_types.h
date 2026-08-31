#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "util/result.h"

namespace can {

struct CanFrame {
    uint32_t id = 0;                 
    bool is_extended = false;
    bool is_remote = false;
    uint8_t dlc = 0;
    std::array<uint8_t, 8> data{};   
};

struct ReceivedFrame {
    CanFrame frame;
    uint64_t timestamp_us = 0;
};

enum class CanErrorCode {
    InvalidArgument,     // 参数非法（如 max_count == 0）
    TxFailed,            // 发送失败
    RxFailed,            // 接收失败
    ClearBufferFailed,   // 清缓冲失败
    DriverError          // SDK/驱动返回的其他错误
};

struct CanError {
    CanErrorCode code = CanErrorCode::DriverError;
    uint32_t native_code = 0;        // SDK 返回的原始错误码
    std::string message;
};

// CAN 传输接口：Encoder 只依赖该接口，不接触厂商 SDK
class ICanBus {
public:
    virtual ~ICanBus() = default;

    virtual util::Result<util::Unit, CanError> send(const CanFrame& frame) = 0;

    virtual util::Result<std::vector<ReceivedFrame>, CanError>
        receive(size_t max_count, std::chrono::milliseconds wait) = 0;

    virtual util::Result<util::Unit, CanError> clearBuffer() = 0;
};

template <typename T>
using CanResult = util::Result<T, CanError>;

} // namespace can
