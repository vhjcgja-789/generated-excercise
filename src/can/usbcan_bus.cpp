#include "can/usbcan_bus.h"

#include "zlgcan.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace can {

// SDK 资源持有者（PImpl）
struct UsbCanBus::Impl {
    DEVICE_HANDLE dev = INVALID_DEVICE_HANDLE;
    CHANNEL_HANDLE chn = INVALID_CHANNEL_HANDLE;
};

namespace {

CanError makeError(CanErrorCode code, uint32_t native, std::string message) {
    CanError e;
    e.code = code;
    e.native_code = native;
    e.message = std::move(message);
    return e;
}

// 项目帧 -> SDK 发送对象
ZCAN_Transmit_Data toZlg(const CanFrame& frame) {
    ZCAN_Transmit_Data tx{};
    tx.frame.can_id = MAKE_CAN_ID(frame.id, frame.is_extended, frame.is_remote, 0);
    tx.frame.can_dlc = frame.dlc;
    std::memcpy(tx.frame.data, frame.data.data(), frame.data.size());
    tx.transmit_type = 0;   // 0 = 正常发送
    return tx;
}

// SDK 接收对象 -> 项目接收帧
ReceivedFrame fromZlg(const ZCAN_Receive_Data& rx) {
    ReceivedFrame out;
    out.frame.id = GET_ID(rx.frame.can_id);
    out.frame.is_extended = IS_EFF(rx.frame.can_id) != 0;
    out.frame.is_remote = IS_RTR(rx.frame.can_id) != 0;
    out.frame.dlc = rx.frame.can_dlc;
    std::memcpy(out.frame.data.data(), rx.frame.data, sizeof(out.frame.data));
    out.timestamp_us = rx.timestamp;
    return out;
}

} // namespace

UsbCanBus::UsbCanBus(Config config) : impl_(std::make_unique<Impl>()) {
    struct Guard {
        DEVICE_HANDLE dev = INVALID_DEVICE_HANDLE;
        CHANNEL_HANDLE chn = INVALID_CHANNEL_HANDLE;
        ~Guard() {
            if (chn != INVALID_CHANNEL_HANDLE) {
                ZCAN_ResetCAN(chn);       
            }
            if (dev != INVALID_DEVICE_HANDLE) {
                ZCAN_CloseDevice(dev);       
            }
        }
    } guard;

    guard.dev = ZCAN_OpenDevice(ZCAN_USBCAN2, config.device_index, 0);
    if (guard.dev == INVALID_DEVICE_HANDLE) {
        throw std::runtime_error(
            "打开设备失败（设备索引 " + std::to_string(config.device_index) +
            "）：请检查驱动是否安装、USB 是否连接、设备是否被其他程序占用");
    }

    char path[24] = {0};
    std::snprintf(path, sizeof(path), "%u/baud_rate", config.channel_index);
    char baud[16] = {0};
    std::snprintf(baud, sizeof(baud), "%u", config.baud_rate);
    if (ZCAN_SetValue(guard.dev, path, baud) != STATUS_OK) {
        throw std::runtime_error(
            "设置通道 " + std::to_string(config.channel_index) + " 波特率失败（" +
            std::to_string(config.baud_rate) + " bps）");
    }

    ZCAN_CHANNEL_INIT_CONFIG init{};
    init.can_type = TYPE_CAN;
    init.can.mode = 0;
    init.can.acc_code = config.acc_code;
    init.can.acc_mask = config.acc_mask;
    guard.chn = ZCAN_InitCAN(guard.dev, config.channel_index, &init);
    if (guard.chn == INVALID_CHANNEL_HANDLE) {
        throw std::runtime_error(
            "初始化通道 " + std::to_string(config.channel_index) + " 失败");
    }

    if (ZCAN_StartCAN(guard.chn) != STATUS_OK) {
        throw std::runtime_error(
            "启动通道 " + std::to_string(config.channel_index) + " 失败");
    }

    // 资源提交给 Impl
    impl_->dev = guard.dev;
    impl_->chn = guard.chn;
    guard.dev = INVALID_DEVICE_HANDLE;
    guard.chn = INVALID_CHANNEL_HANDLE;
}


UsbCanBus::~UsbCanBus() {
    if (!impl_) {
        return;
    }
    if (impl_->chn != INVALID_CHANNEL_HANDLE) {
        ZCAN_ResetCAN(impl_->chn);
    }
    if (impl_->dev != INVALID_DEVICE_HANDLE) {
        ZCAN_CloseDevice(impl_->dev);
    }
}

// 发送单帧
CanResult<util::Unit> UsbCanBus::send(const CanFrame& frame) {
    const uint32_t id_max = frame.is_extended ? 0x1FFFFFFFu : 0x7FFu;
    if (frame.id > id_max) {
        return CanResult<util::Unit>::Fail(
            makeError(CanErrorCode::InvalidArgument, 0, "CAN ID 超出范围"));
    }
    if (frame.dlc > frame.data.size()) {
        return CanResult<util::Unit>::Fail(
            makeError(CanErrorCode::InvalidArgument, 0, "DLC 超过 8"));
    }

    ZCAN_Transmit_Data tx = toZlg(frame);
    const UINT sent = ZCAN_Transmit(impl_->chn, &tx, 1);
    if (sent != 1) {
        return CanResult<util::Unit>::Fail(
            makeError(CanErrorCode::TxFailed, sent, "发送 CAN 帧失败"));
    }
    return CanResult<util::Unit>::Ok(util::Unit{});
}

// 接收帧：wait=0 非阻塞；正常超时/无帧返回成功的空 vector；
CanResult<std::vector<ReceivedFrame>>
UsbCanBus::receive(size_t max_count, std::chrono::milliseconds wait) {
    if (max_count == 0) {
        return CanResult<std::vector<ReceivedFrame>>::Fail(
            makeError(CanErrorCode::InvalidArgument, 0, "max_count 不能为 0"));
    }
    if (wait.count() < 0) {
        return CanResult<std::vector<ReceivedFrame>>::Fail(
            makeError(CanErrorCode::InvalidArgument, 0, "wait 不能为负"));
    }

    const size_t batch = std::min<size_t>(max_count, 512);
    const long long ms = std::min<long long>(wait.count(), INT_MAX);
    const int wait_ms = static_cast<int>(ms);

    std::vector<ZCAN_Receive_Data> rx(batch);
    const UINT n = ZCAN_Receive(impl_->chn, rx.data(),
                                static_cast<UINT>(batch), wait_ms);

    std::vector<ReceivedFrame> out;
    out.reserve(n);
    for (UINT i = 0; i < n; ++i) {
        out.push_back(fromZlg(rx[i]));
    }
    return CanResult<std::vector<ReceivedFrame>>::Ok(std::move(out));
}

// 清空接收缓冲
CanResult<util::Unit> UsbCanBus::clearBuffer() {
    const UINT ret = ZCAN_ClearBuffer(impl_->chn);
    if (ret != STATUS_OK) {
        return CanResult<util::Unit>::Fail(
            makeError(CanErrorCode::ClearBufferFailed, ret, "清空接收缓冲失败"));
    }
    return CanResult<util::Unit>::Ok(util::Unit{});
}

} // namespace can
