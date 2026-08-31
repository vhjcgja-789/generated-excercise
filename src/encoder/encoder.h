#pragma once

#include <array>
#include <cstdint>

#include "encoder/encoder_types.h"

namespace enc {

class Encoder {
public:
    Encoder(can::ICanBus& bus) : Encoder(bus, EncoderConfig{}) {}
    explicit Encoder(can::ICanBus& bus, EncoderConfig config);

    // ---- 查询 ----
    EncResult<uint32_t> readValue();
    EncResult<int32_t> readVelocity();

    // ---- 配置 ----
    EncResult<util::Unit> setId(uint8_t id);
    EncResult<util::Unit> setBaudRate(BaudRate rate);
    EncResult<util::Unit> setMode(EncoderMode mode);
    EncResult<util::Unit> setAutoReportTime(std::chrono::microseconds time);
    EncResult<util::Unit> setZero();
    EncResult<util::Unit> setDirection(Direction direction);
    EncResult<util::Unit> setVelocitySampleTime(std::chrono::milliseconds time);
    EncResult<util::Unit> setMidpoint();
    EncResult<util::Unit> setValue(uint32_t value);
    EncResult<util::Unit> setFiveTurns();

    const EncoderAddress& address() const noexcept { return address_; }
    EncoderMode mode() const noexcept { return mode_; }

private:
    static const CommandSpec& spec(Command command);

    EncResult<can::ReceivedFrame> transact(const CommandSpec& s,
                                           std::array<uint8_t, 4> payload);

    can::ICanBus& bus_;        
    EncoderConfig config_;
    EncoderAddress address_;
    EncoderMode mode_ = EncoderMode::Query;
};

} // namespace enc
