# -*- coding: utf-8 -*-
"""设备工作线程：全部 DLL 调用都在本线程串行执行，结果经信号回主线程。

主线程发信号 -> worker 槽（跨线程自动排队，天然串行化）-> 结果信号回主线程。
"""
from PyQt5.QtCore import QObject, pyqtSignal, pyqtSlot

from .encoder_core import CoreError, Device


class DeviceWorker(QObject):
    # 指令名, 是否成功, 消息, 值（仅读取类指令有效）
    sig_cmd_done = pyqtSignal(str, bool, str, int)
    # 编码器值, 角速度, 错误（空串表示正常）
    sig_poll = pyqtSignal(int, int, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._device = None

    @pyqtSlot(tuple)
    def do_open(self, cfg):
        # cfg = (device_index, channel_index, baud_rate, encoder_id)
        try:
            self._device = Device(*cfg)
            self.sig_cmd_done.emit("open", True, "设备打开成功", 0)
        except CoreError as e:
            self._device = None
            self.sig_cmd_done.emit("open", False, str(e), 0)
        except Exception as e:
            self._device = None
            self.sig_cmd_done.emit("open", False, f"内部错误: {e}", 0)

    @pyqtSlot()
    def do_close(self):
        if self._device:
            self._device.close()
            self._device = None
        self.sig_cmd_done.emit("close", True, "设备已断开", 0)

    @pyqtSlot(str, tuple)
    def do_cmd(self, name, args):
        if self._device is None:
            self.sig_cmd_done.emit(name, False, "未连接设备", 0)
            return
        try:
            value = 0
            if name == "read_value":
                value = self._device.read_value()
            elif name == "read_velocity":
                value = self._device.read_velocity()
            elif name == "set_id":
                self._device.set_id(args[0])
            elif name == "set_baud_rate":
                self._device.set_baud_rate(args[0])
            elif name == "set_mode":
                self._device.set_mode(args[0])
            elif name == "set_auto_report_time":
                self._device.set_auto_report_time(args[0])
            elif name == "set_zero":
                self._device.set_zero()
            elif name == "set_direction":
                self._device.set_direction(args[0])
            elif name == "set_velocity_sample_time":
                self._device.set_velocity_sample_time(args[0])
            elif name == "set_midpoint":
                self._device.set_midpoint()
            elif name == "set_value":
                self._device.set_value(args[0])
            elif name == "set_five_turns":
                self._device.set_five_turns()
            else:
                self.sig_cmd_done.emit(name, False, f"未知指令: {name}", 0)
                return
            self.sig_cmd_done.emit(name, True, "成功", value)
        except CoreError as e:
            self.sig_cmd_done.emit(name, False, str(e), 0)
        except Exception as e:
            self.sig_cmd_done.emit(name, False, f"内部错误: {e}", 0)

    @pyqtSlot()
    def do_poll(self):
        if self._device is None:
            return
        try:
            value = self._device.read_value()
            velocity = self._device.read_velocity()
            self.sig_poll.emit(value, velocity, "")
        except CoreError as e:
            self.sig_poll.emit(0, 0, str(e))
        except Exception as e:
            self.sig_poll.emit(0, 0, f"内部错误: {e}")
