#pragma once

#include <memory>

#include "can/can_types.h"

namespace can {

class UsbCanBus final : public ICanBus {
public:
    struct Config {
        uint32_t device_index = 0;    
        uint32_t channel_index = 1;   // 编码器接通道
        uint32_t baud_rate = 500000;  // 编码器波特率
        uint32_t acc_code = 0;
        uint32_t acc_mask = 0xFFFFFFFFu;   // 滤波
    };

    UsbCanBus() : UsbCanBus(Config{}) {}
    explicit UsbCanBus(Config config);
    ~UsbCanBus() override;

    UsbCanBus(const UsbCanBus&) = delete;
    UsbCanBus& operator=(const UsbCanBus&) = delete;

    // ICanBus
    CanResult<util::Unit> send(const CanFrame& frame) override;
    CanResult<std::vector<ReceivedFrame>>
        receive(size_t max_count, std::chrono::milliseconds wait) override;
    CanResult<util::Unit> clearBuffer() override;

private:
    struct Impl;                    // 定义在 usbcan_bus.cpp，持有 SDK 句柄
    std::unique_ptr<Impl> impl_;
};

} // namespace can
