# -*- coding: utf-8 -*-
"""无 GUI 冒烟测试：验证 encoder_core.dll 与 ctypes 边界（不需要硬件）。

验证点：DLL 加载、调用约定（cdecl/64 位）、结构体布局、错误文本通道。
无硬件时 enc_open 会优雅失败并给出中文错误 —— 走到这一步即说明边界正常；
成功路径需连接硬件后在 GUI 中验证。

运行：python -m bindings.pyqt.smoke_test
"""
from .encoder_core import CoreError, Device


def main():
    print("== encoder_core.dll 边界冒烟测试 ==")
    try:
        d = Device(device_index=0, channel_index=1, baud_rate=500000,
                   encoder_id=1)
        print("设备打开成功（检测到硬件）")
        print("编码器值 =", d.read_value())
        print("角速度 =", d.read_velocity())
        d.close()
        print("关闭成功")
    except CoreError as e:
        # 无硬件时预期走到这里：证明 DLL 加载、调用约定、错误文本均正常
        print("打开/操作失败（未接硬件时属预期）：", e)
    except Exception as e:
        print("意外异常（边界有问题）：", type(e).__name__, e)
        raise SystemExit(1)
    print("== 冒烟测试结束 ==")


if __name__ == "__main__":
    main()
