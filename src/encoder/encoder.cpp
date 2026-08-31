#include "encoder/encoder.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace enc {

namespace {

std::string hex2(uint8_t v) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s;
    s += digits[v >> 4];
    s += digits[v & 0x0F];
    return s;
}

EncoderError makeError(EncoderErrorCode code, std::string message) {
    EncoderError e;
    e.code = code;
    e.message = std::move(message);
    return e;
}

enum class Match { Hit, Bad, Ignore };

Match classify(const can::ReceivedFrame& rf, const CommandSpec& s,
               const EncoderAddress& expected) {
    const can::CanFrame& f = rf.frame;

    if (f.dlc < 3) {
        return f.id == expected.can_id ? Match::Bad : Match::Ignore;
    }

    const bool id_ok = (f.id == expected.can_id);
    const bool proto_ok = (f.data[1] == expected.protocol_id);
    const bool func_ok = (f.data[2] == static_cast<uint8_t>(s.command));

    if (!id_ok && !proto_ok) {
        return Match::Ignore;
    }

    if (!id_ok || !proto_ok || !func_ok || f.is_remote ||
        f.is_extended != expected.is_extended ||
        f.dlc != s.response_dlc || f.data[0] != f.dlc) {
        return Match::Bad;
    }
    return Match::Hit;
}

EncResult<util::Unit> parseStatus(const can::ReceivedFrame& rf) {
    const uint8_t status = rf.frame.data[3];
    if (status == 0) {
        return EncResult<util::Unit>::Ok(util::Unit{});
    }
    EncoderError e = makeError(
        EncoderErrorCode::EncoderRejected,
        "设备拒绝该指令，错误码 0x" + hex2(status));
    e.encoder_status = status;   
    return EncResult<util::Unit>::Fail(std::move(e));
}

EncResult<uint32_t> parseU32(const can::ReceivedFrame& rf) {
    const uint8_t* d = rf.frame.data.data();
    const uint32_t v = static_cast<uint32_t>(d[3]) |
                       (static_cast<uint32_t>(d[4]) << 8) |
                       (static_cast<uint32_t>(d[5]) << 16) |
                       (static_cast<uint32_t>(d[6]) << 24);
    return EncResult<uint32_t>::Ok(v);
}

EncResult<int32_t> parseI32(const can::ReceivedFrame& rf) {
    const uint32_t u = parseU32(rf).value();
    int32_t v = 0;
    std::memcpy(&v, &u, sizeof(v));   
    return EncResult<int32_t>::Ok(v);
}

} // namespace

Encoder::Encoder(can::ICanBus& bus, EncoderConfig config)
    : bus_(bus), config_(config), address_(config.address) {}


const CommandSpec& Encoder::spec(Command command) {
    for (const CommandSpec& s : kCommandTable) {
        if (s.command == command) {
            return s;
        }
    }
    throw std::logic_error("指令不在规格表中");
}

EncResult<can::ReceivedFrame> Encoder::transact(const CommandSpec& s,
                                                std::array<uint8_t, 4> payload) {
    // 1. 组请求帧：DLC = LEN = 3 + payload
    can::CanFrame req;
    req.id = address_.can_id;
    req.is_extended = address_.is_extended;
    req.is_remote = false;
    req.dlc = static_cast<uint8_t>(3 + s.request_payload_size);
    req.data[0] = req.dlc;
    req.data[1] = address_.protocol_id;
    req.data[2] = static_cast<uint8_t>(s.command);
    for (uint8_t i = 0; i < s.request_payload_size; ++i) {
        req.data[3 + i] = payload[i];
    }

    // 2. 期望应答地址：setId 成功后从机改用新地址应答
    EncoderAddress expected = address_;
    if (s.address_policy == AddressPolicy::NewId) {
        expected = EncoderAddress::standard(payload[0]);
    }

    // 3. 发送
    auto sent = bus_.send(req);
    if (!sent) {
        EncoderError e = makeError(
            EncoderErrorCode::Transport,
            "指令 0x" + hex2(req.data[2]) + " (" + s.name + ") 发送失败");
        e.transport = sent.error();
        return EncResult<can::ReceivedFrame>::Fail(std::move(e));
    }

    // 4. 等待应答
    const auto deadline = std::chrono::steady_clock::now() + config_.timeout;
    for (;;) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            break;
        }

        auto got = bus_.receive(16, remaining);
        if (!got) {
            EncoderError e = makeError(
                EncoderErrorCode::Transport,
                "指令 0x" + hex2(req.data[2]) + " (" + s.name + ") 接收失败");
            e.transport = got.error();
            return EncResult<can::ReceivedFrame>::Fail(std::move(e));
        }

        for (const can::ReceivedFrame& rf : got.value()) {
            switch (classify(rf, s, expected)) {
            case Match::Hit:
                return EncResult<can::ReceivedFrame>::Ok(rf);
            case Match::Bad:
                return EncResult<can::ReceivedFrame>::Fail(makeError(
                    EncoderErrorCode::BadResponse,
                    "指令 0x" + hex2(req.data[2]) + " (" + s.name +
                        ") 应答帧校验失败（帧类型/DLC/LEN/ID/FUNC 不符）"));
            case Match::Ignore:
                break;   
            }
        }
    }

    // 5. 超时
    std::string msg =
        "指令 0x" + hex2(req.data[2]) + " (" + s.name + ") 无应答（超时）";
    if (s.command == Command::SetId) {
        msg += "：新地址可能已生效，请按新地址探测";
    } else if (s.command == Command::SetBaud) {
        msg += "：波特率可能已切换，请关闭总线按新波特率重建";
    } else {
        msg += "：检查接线/120Ω终端电阻/波特率/编码器实际地址";
    }
    return EncResult<can::ReceivedFrame>::Fail(
        makeError(EncoderErrorCode::Timeout, std::move(msg)));
}

EncResult<uint32_t> Encoder::readValue() {
    std::array<uint8_t, 4> payload{};
    auto r = transact(spec(Command::ReadValue), payload);
    if (!r) {
        return EncResult<uint32_t>::Fail(r.error());
    }
    return parseU32(r.value());
}

EncResult<int32_t> Encoder::readVelocity() {
    std::array<uint8_t, 4> payload{};
    auto r = transact(spec(Command::ReadVelocity), payload);
    if (!r) {
        return EncResult<int32_t>::Fail(r.error());
    }
    return parseI32(r.value());
}

EncResult<util::Unit> Encoder::setId(uint8_t id) {
    if (id < 1) {
        return EncResult<util::Unit>::Fail(
            makeError(EncoderErrorCode::InvalidArgument, "setId 参数范围 1~255"));
    }
    std::array<uint8_t, 4> payload{};
    payload[0] = id;
    auto r = transact(spec(Command::SetId), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    auto st = parseStatus(r.value());
    if (!st) {
        return st;   
    }
    address_ = EncoderAddress::standard(id);   
    return EncResult<util::Unit>::Ok(util::Unit{});
}

EncResult<util::Unit> Encoder::setBaudRate(BaudRate rate) {
    if (static_cast<uint8_t>(rate) > static_cast<uint8_t>(BaudRate::K100)) {
        return EncResult<util::Unit>::Fail(makeError(
            EncoderErrorCode::InvalidArgument, "setBaudRate 参数超出 0~4"));
    }
    std::array<uint8_t, 4> payload{};
    payload[0] = static_cast<uint8_t>(rate);
    auto r = transact(spec(Command::SetBaud), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit> Encoder::setMode(EncoderMode mode) {
    switch (mode) {
    case EncoderMode::Query:
    case EncoderMode::AutoValueStd:
    case EncoderMode::AutoVelSignedStd:
    case EncoderMode::AutoVelUnsignedStd:
    case EncoderMode::AutoValueExt:
    case EncoderMode::AutoVelSignedExt:
    case EncoderMode::AutoVelUnsignedExt:
        break;
    default:
        return EncResult<util::Unit>::Fail(makeError(
            EncoderErrorCode::InvalidArgument, "setMode 参数不是合法模式值"));
    }
    std::array<uint8_t, 4> payload{};
    payload[0] = static_cast<uint8_t>(mode);
    auto r = transact(spec(Command::SetMode), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    auto st = parseStatus(r.value());
    if (!st) {
        return st;
    }
    mode_ = mode;  
    return EncResult<util::Unit>::Ok(util::Unit{});
}

EncResult<util::Unit>
Encoder::setAutoReportTime(std::chrono::microseconds time) {
    const long long us = time.count();
    if (us < 50 || us > 65535) {
        return EncResult<util::Unit>::Fail(makeError(
            EncoderErrorCode::InvalidArgument,
            "setAutoReportTime 范围 50~65535 微秒"));
    }
    const uint16_t v = static_cast<uint16_t>(us);
    std::array<uint8_t, 4> payload{};
    payload[0] = static_cast<uint8_t>(v & 0xFF);
    payload[1] = static_cast<uint8_t>(v >> 8);
    auto r = transact(spec(Command::SetAutoTime), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit> Encoder::setZero() {
    std::array<uint8_t, 4> payload{};
    payload[0] = 0x00;
    auto r = transact(spec(Command::SetZero), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit> Encoder::setDirection(Direction direction) {
    if (static_cast<uint8_t>(direction) > 1) {
        return EncResult<util::Unit>::Fail(makeError(
            EncoderErrorCode::InvalidArgument, "setDirection 参数只能是 0/1"));
    }
    std::array<uint8_t, 4> payload{};
    payload[0] = static_cast<uint8_t>(direction);
    auto r = transact(spec(Command::SetDirection), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit>
Encoder::setVelocitySampleTime(std::chrono::milliseconds time) {
    const long long ms = time.count();
    if (ms < 0 || ms > 65535) {
        return EncResult<util::Unit>::Fail(makeError(
            EncoderErrorCode::InvalidArgument,
            "setVelocitySampleTime 范围 0~65535 毫秒"));
    }
    const uint16_t v = static_cast<uint16_t>(ms);
    std::array<uint8_t, 4> payload{};
    payload[0] = static_cast<uint8_t>(v & 0xFF);
    payload[1] = static_cast<uint8_t>(v >> 8);
    auto r = transact(spec(Command::SetVelocitySampleTime), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit> Encoder::setMidpoint() {
    std::array<uint8_t, 4> payload{};
    payload[0] = 0x01;
    auto r = transact(spec(Command::SetMidpoint), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit> Encoder::setValue(uint32_t value) {
    std::array<uint8_t, 4> payload{};
    payload[0] = static_cast<uint8_t>(value & 0xFF);
    payload[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>(value >> 24);
    auto r = transact(spec(Command::SetValue), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

EncResult<util::Unit> Encoder::setFiveTurns() {
    std::array<uint8_t, 4> payload{};
    payload[0] = 0x01;
    auto r = transact(spec(Command::SetFiveTurns), payload);
    if (!r) {
        return EncResult<util::Unit>::Fail(r.error());
    }
    return parseStatus(r.value());
}

} // namespace enc
