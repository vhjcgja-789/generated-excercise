#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <chrono>
#include <exception>
#include <thread>

#include "can/usbcan_bus.h"
#include "encoder/encoder.h"

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::setvbuf(stdout, nullptr, _IONBF, 0);

    try {
        can::UsbCanBus::Config cfg;
        cfg.device_index = 0;
        cfg.channel_index = 1;
        cfg.baud_rate = 500000;
        can::UsbCanBus bus(cfg);
        enc::Encoder enc(bus);

        std::printf("设备打开成功：通道1 @ %d bps，编码器地址 %u\n\n",
                    cfg.baud_rate, enc.address().can_id);

        auto cleared = bus.clearBuffer();
        if (!cleared) {
            std::printf("警告：清空接收缓冲失败 - %s\n",
                        cleared.error().message.c_str());
        }

        auto id = enc.setId(1);
        if (id) {
            std::printf("设置编码器 ID = 1：成功（当前地址 %u）\n",
                        enc.address().can_id);
        } else {
            std::printf("设置编码器 ID = 1 失败：%s\n",
                        id.error().message.c_str());
        }

        auto zero = enc.setZero();
        if (zero) {
            std::printf("设置零点（当前位置置 0）：成功\n");
        } else {
            std::printf("设置零点失败：%s\n", zero.error().message.c_str());
        }

        std::printf("\n开始轮询（每 500ms 一次），按任意键退出...\n");
        while (!_kbhit()) {
            auto v = enc.readValue();
            if (v) {
                std::printf("编码器值 = %u", v.value());
            } else {
                std::printf("读取值失败 - %s", v.error().message.c_str());
            }

            auto vel = enc.readVelocity();
            if (vel) {
                std::printf(" | 角速度 = %d\n", vel.value());
            } else {
                std::printf(" | 读速度失败 - %s\n", vel.error().message.c_str());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        (void)_getch();   
    } catch (const std::exception& e) {
        std::printf("初始化失败：%s\n", e.what());
        std::printf("请检查：驱动是否安装、USB 是否连接、设备是否被其他程序占用。\n");
        std::printf("按任意键退出...\n");
        (void)_getch();
        return 1;
    }
    return 0;
}
