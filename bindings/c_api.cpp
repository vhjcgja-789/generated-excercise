#include "c_api.h"

#include <chrono>
#include <memory>
#include <string>

#include "can/usbcan_bus.h"
#include "encoder/encoder.h"

namespace {

thread_local std::string g_last_error;

// 句柄宿主：UsbCanBus 在前、Encoder 在后（Encoder 持有总线引用）
struct Core {
    can::UsbCanBus bus;
    enc::Encoder enc;

    explicit Core(const enc_config_t& cfg)
        : bus(can::UsbCanBus::Config{cfg.device_index, cfg.channel_index,
                                     cfg.baud_rate}),
          enc(bus, enc::EncoderConfig{
                       enc::EncoderAddress::standard(cfg.encoder_id),
                       std::chrono::milliseconds{1000}}) {}
};

Core* asCore(void* h) { return static_cast<Core*>(h); }

int32_t mapError(const enc::EncoderError& e) {
    g_last_error = e.message;
    switch (e.code) {
    case enc::EncoderErrorCode::InvalidArgument: return ENC_ERR_INVALID_ARG;
    case enc::EncoderErrorCode::Transport:       return ENC_ERR_TRANSPORT;
    case enc::EncoderErrorCode::Timeout:         return ENC_ERR_TIMEOUT;
    case enc::EncoderErrorCode::BadResponse:     return ENC_ERR_BAD_RESPONSE;
    case enc::EncoderErrorCode::EncoderRejected: return ENC_ERR_REJECTED;
    }
    return ENC_ERR_UNKNOWN;
}

// 统一入口：句柄检查 + 吞噬全部异常，DLL 边界不漏异常
template <typename Fn>
int32_t guard(void* h, Fn&& fn) {
    if (!h) {
        g_last_error = "未连接设备（句柄为空）";
        return ENC_ERR_INVALID_ARG;
    }
    try {
        return fn(*asCore(h));
    } catch (const std::exception& e) {
        g_last_error = e.what();
    } catch (...) {
        g_last_error = "未知内部异常";
    }
    return ENC_ERR_UNKNOWN;
}

} // namespace

void* enc_open(const enc_config_t* cfg) {
    if (!cfg) {
        g_last_error = "配置参数为空";
        return nullptr;
    }
    try {
        return new Core(*cfg);
    } catch (const std::exception& e) {
        g_last_error = e.what();
    } catch (...) {
        g_last_error = "打开设备时发生未知异常";
    }
    return nullptr;
}

void enc_close(void* h) { delete asCore(h); }

const char* enc_last_error() { return g_last_error.c_str(); }

int32_t enc_read_value(void* h, uint32_t* out) {
    if (!out) {
        g_last_error = "输出参数为空";
        return ENC_ERR_INVALID_ARG;
    }
    return guard(h, [out](Core& c) {
        auto r = c.enc.readValue();
        if (!r) return mapError(r.error());
        *out = r.value();
        return ENC_OK;
    });
}

int32_t enc_set_id(void* h, uint8_t id) {
    return guard(h, [id](Core& c) {
        auto r = c.enc.setId(id);
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_baud_rate(void* h, uint8_t rate) {
    return guard(h, [rate](Core& c) {
        auto r = c.enc.setBaudRate(static_cast<enc::BaudRate>(rate));
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_mode(void* h, uint8_t mode) {
    return guard(h, [mode](Core& c) {
        auto r = c.enc.setMode(static_cast<enc::EncoderMode>(mode));
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_auto_report_time(void* h, uint16_t us) {
    return guard(h, [us](Core& c) {
        auto r = c.enc.setAutoReportTime(std::chrono::microseconds{us});
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_zero(void* h) {
    return guard(h, [](Core& c) {
        auto r = c.enc.setZero();
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_direction(void* h, uint8_t direction) {
    return guard(h, [direction](Core& c) {
        auto r = c.enc.setDirection(static_cast<enc::Direction>(direction));
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_read_velocity(void* h, int32_t* out) {
    if (!out) {
        g_last_error = "输出参数为空";
        return ENC_ERR_INVALID_ARG;
    }
    return guard(h, [out](Core& c) {
        auto r = c.enc.readVelocity();
        if (!r) return mapError(r.error());
        *out = r.value();
        return ENC_OK;
    });
}

int32_t enc_set_velocity_sample_time(void* h, uint16_t ms) {
    return guard(h, [ms](Core& c) {
        auto r = c.enc.setVelocitySampleTime(std::chrono::milliseconds{ms});
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_midpoint(void* h) {
    return guard(h, [](Core& c) {
        auto r = c.enc.setMidpoint();
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_value(void* h, uint32_t value) {
    return guard(h, [value](Core& c) {
        auto r = c.enc.setValue(value);
        return r ? ENC_OK : mapError(r.error());
    });
}

int32_t enc_set_five_turns(void* h) {
    return guard(h, [](Core& c) {
        auto r = c.enc.setFiveTurns();
        return r ? ENC_OK : mapError(r.error());
    });
}
