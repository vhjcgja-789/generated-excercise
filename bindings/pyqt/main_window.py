# -*- coding: utf-8 -*-
"""PyQt5 主窗口：连接区 + 实时轮询区 + 12 条指令面板 + 日志。"""
from datetime import datetime

from PyQt5.QtCore import QRegularExpression, QThread, QTimer, pyqtSignal
from PyQt5.QtGui import QRegularExpressionValidator
from PyQt5.QtWidgets import (
    QCheckBox, QComboBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel,
    QLineEdit, QPlainTextEdit, QPushButton, QSpinBox, QVBoxLayout, QWidget,
)

from .device_worker import DeviceWorker

BAUD_ITEMS = [
    (500000, "500K（出厂默认）"),
    (1000000, "1M"),
    (250000, "250K"),
    (125000, "125K"),
    (100000, "100K"),
]

MODE_ITEMS = [
    (0x00, "查询（推荐）"),
    (0xAA, "自动回传值·标准帧"),
    (0x02, "自动回传有符号角速度·标准帧"),
    (0x07, "自动回传无符号角速度·标准帧"),
    (0x18, "自动回传值·扩展帧"),
    (0x12, "自动回传有符号角速度·扩展帧"),
    (0x17, "自动回传无符号角速度·扩展帧"),
]

DIR_ITEMS = [(0, "顺时针"), (1, "逆时针")]


class MainWindow(QWidget):
    # 发往 worker 线程的请求信号（跨线程自动排队 = 串行化）
    req_open = pyqtSignal(tuple)
    req_close = pyqtSignal()
    req_cmd = pyqtSignal(str, tuple)
    req_poll = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.setWindowTitle("EncoderDemo 上位机 — USBCAN-II × 布瑞特多圈编码器")
        self._connected = False
        self._busy = False
        self._last_poll_err = ""

        self._build_ui()
        self._build_worker()
        self._set_panels_enabled(False)

    # ---------------- UI ----------------
    def _build_ui(self):
        root = QVBoxLayout(self)

        # 连接区
        grp_conn = QGroupBox("连接")
        lay = QHBoxLayout(grp_conn)
        self.spin_dev = QSpinBox()
        self.spin_dev.setRange(0, 3)
        self.spin_dev.setValue(0)
        self.spin_ch = QSpinBox()
        self.spin_ch.setRange(0, 1)
        self.spin_ch.setValue(1)
        self.combo_baud = QComboBox()
        for value, text in BAUD_ITEMS:
            self.combo_baud.addItem(text, value)
        self.spin_id = QSpinBox()
        self.spin_id.setRange(1, 255)
        self.spin_id.setValue(1)
        self.btn_conn = QPushButton("连接")
        self.btn_conn.clicked.connect(self._on_connect)
        lay.addWidget(QLabel("设备索引"))
        lay.addWidget(self.spin_dev)
        lay.addWidget(QLabel("通道"))
        lay.addWidget(self.spin_ch)
        lay.addWidget(QLabel("波特率"))
        lay.addWidget(self.combo_baud)
        lay.addWidget(QLabel("编码器地址"))
        lay.addWidget(self.spin_id)
        lay.addWidget(self.btn_conn)
        lay.addStretch(1)
        root.addWidget(grp_conn)

        # 实时区
        self.grp_live = QGroupBox("实时显示（轮询 0x01 编码器值 / 0x0A 角速度）")
        lay = QHBoxLayout(self.grp_live)
        self.edt_value = QLineEdit("--")
        self.edt_value.setReadOnly(True)
        self.edt_vel = QLineEdit("--")
        self.edt_vel.setReadOnly(True)
        self.chk_auto = QCheckBox("自动轮询")
        self.chk_auto.setChecked(True)
        self.chk_auto.toggled.connect(self._on_auto_toggled)
        self.spin_period = QSpinBox()
        self.spin_period.setRange(100, 5000)
        self.spin_period.setValue(500)
        self.spin_period.setSuffix(" ms")
        lay.addWidget(QLabel("编码器值"))
        lay.addWidget(self.edt_value, 1)
        lay.addWidget(QLabel("角速度"))
        lay.addWidget(self.edt_vel, 1)
        lay.addWidget(self.chk_auto)
        lay.addWidget(self.spin_period)
        root.addWidget(self.grp_live)

        # 指令面板（12 条）
        self.grp_cmd = QGroupBox("指令面板（12 条）")
        grid = QGridLayout(self.grp_cmd)
        self.spin_setid = QSpinBox()
        self.spin_setid.setRange(1, 255)
        self.spin_setid.setValue(2)
        self.combo_setbaud = QComboBox()
        for value, text in BAUD_ITEMS:
            self.combo_setbaud.addItem(text, value)
        self.combo_setmode = QComboBox()
        for value, text in MODE_ITEMS:
            self.combo_setmode.addItem(text, value)
        self.spin_autotime = QSpinBox()
        self.spin_autotime.setRange(50, 65535)
        self.spin_autotime.setValue(1000)
        self.spin_autotime.setSuffix(" µs")
        self.combo_dir = QComboBox()
        for value, text in DIR_ITEMS:
            self.combo_dir.addItem(text, value)
        self.spin_veltime = QSpinBox()
        self.spin_veltime.setRange(0, 65535)
        self.spin_veltime.setValue(100)
        self.spin_veltime.setSuffix(" ms")
        self.edt_setvalue = QLineEdit("0")
        self.edt_setvalue.setValidator(
            QRegularExpressionValidator(QRegularExpression(r"\d{1,10}")))

        rows = [
            ("0x01 读取编码器值", None, "读取一次",
             lambda: self._send("read_value", ())),
            ("0x02 设置 ID（1~255，成功后按新地址应答）", self.spin_setid, "下发",
             lambda: self._send("set_id", (self.spin_setid.value(),))),
            ("0x03 设置波特率（切换后需按新波特率重连）", self.combo_setbaud, "下发",
             lambda: self._send("set_baud_rate", (self.combo_setbaud.currentData(),))),
            ("0x04 设置工作模式（自动回传会干扰轮询）", self.combo_setmode, "下发",
             lambda: self._send("set_mode", (self.combo_setmode.currentData(),))),
            ("0x05 设置自动回传时间（50~65535 µs）", self.spin_autotime, "下发",
             lambda: self._send("set_auto_report_time", (self.spin_autotime.value(),))),
            ("0x06 当前位置置零点", None, "下发",
             lambda: self._send("set_zero", ())),
            ("0x07 设置递增方向", self.combo_dir, "下发",
             lambda: self._send("set_direction", (self.combo_dir.currentData(),))),
            ("0x0A 读取角速度（i32，有符号）", None, "读取一次",
             lambda: self._send("read_velocity", ())),
            ("0x0B 设置角速度采样时间（0~65535 ms）", self.spin_veltime, "下发",
             lambda: self._send("set_velocity_sample_time", (self.spin_veltime.value(),))),
            ("0x0C 当前位置置中点", None, "下发",
             lambda: self._send("set_midpoint", ())),
            ("0x0D 设置当前位置值（0~4294967295）", self.edt_setvalue, "下发",
             self._send_set_value),
            ("0x0F 当前位置置 5 圈值", None, "下发",
             lambda: self._send("set_five_turns", ())),
        ]
        for r, (title, widget, btn_text, handler) in enumerate(rows):
            grid.addWidget(QLabel(title), r, 0)
            if widget is not None:
                grid.addWidget(widget, r, 1)
            btn = QPushButton(btn_text)
            btn.clicked.connect(handler)
            grid.addWidget(btn, r, 2)
        root.addWidget(self.grp_cmd)

        # 日志
        self.txt_log = QPlainTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setMaximumBlockCount(2000)
        root.addWidget(self.txt_log, 1)

        # 轮询定时器
        self._timer = QTimer(self)
        self._timer.setInterval(500)
        self._timer.timeout.connect(self.req_poll)
        self.spin_period.valueChanged.connect(self._timer.setInterval)

    def _build_worker(self):
        self._thread = QThread(self)
        self._worker = DeviceWorker()
        self._worker.moveToThread(self._thread)
        self.req_open.connect(self._worker.do_open)
        self.req_close.connect(self._worker.do_close)
        self.req_cmd.connect(self._worker.do_cmd)
        self.req_poll.connect(self._worker.do_poll)
        self._worker.sig_cmd_done.connect(self._on_cmd_done)
        self._worker.sig_poll.connect(self._on_poll)
        self._thread.finished.connect(self._worker.deleteLater)
        self._thread.start()

    # ---------------- 交互 ----------------
    def _on_connect(self):
        if self._busy:
            return
        if self._connected:
            self._busy = True
            self._log("断开中...")
            self.req_close.emit()
            return
        cfg = (self.spin_dev.value(), self.spin_ch.value(),
               self.combo_baud.currentData(), self.spin_id.value())
        self._busy = True
        self._log(f"打开中（设备 {cfg[0]}，通道 {cfg[1]}，{cfg[2]} bps，"
                  f"编码器地址 {cfg[3]}）...")
        self.req_open.emit(cfg)

    def _send(self, name, args):
        if not self._connected:
            self._log(f"[{name}] 未连接设备")
            return
        self.req_cmd.emit(name, args)

    def _send_set_value(self):
        if not self._connected:
            self._log("[set_value] 未连接设备")
            return
        text = self.edt_setvalue.text().strip()
        if not text.isdigit():
            self._log("[set_value] 请输入十进制数")
            return
        value = int(text)
        if value > 0xFFFFFFFF:
            self._log("[set_value] 超出 0~4294967295")
            return
        self.req_cmd.emit("set_value", (value,))

    def _on_auto_toggled(self, checked):
        if checked and self._connected:
            self._timer.start()
        else:
            self._timer.stop()

    def _start_polling(self):
        self._timer.setInterval(self.spin_period.value())
        if self.chk_auto.isChecked():
            self._timer.start()

    def _stop_polling(self):
        self._timer.stop()

    def _set_panels_enabled(self, enabled):
        self.grp_live.setEnabled(enabled)
        self.grp_cmd.setEnabled(enabled)

    # ---------------- worker 回调 ----------------
    def _on_cmd_done(self, name, ok, message, value):
        self._busy = False
        if name == "open":
            self._connected = ok
            self._set_panels_enabled(ok)
            self.btn_conn.setText("断开连接" if ok else "连接")
            if ok:
                self._start_polling()
            self._log(f"[连接] {message}")
            return
        if name == "close":
            self._connected = False
            self._set_panels_enabled(False)
            self.btn_conn.setText("连接")
            self._log(f"[断开] {message}")
            return
        if ok:
            if name == "read_value":
                self.edt_value.setText(str(value))
                self._log(f"[0x01 读取编码器值] 成功：{value}")
            elif name == "read_velocity":
                self.edt_vel.setText(str(value))
                self._log(f"[0x0A 读取角速度] 成功：{value}")
            else:
                self._log(f"[{name}] 成功")
                if name == "set_baud_rate":
                    self._log("提示：编码器波特率已切换，如需继续操作请断开后把"
                              "「波特率」切到新值再重连。")
                elif name == "set_mode" and self.combo_setmode.currentData() != 0x00:
                    self._log("提示：已进入自动回传模式，编码器会主动上报帧，"
                              "可能干扰轮询；演示完请切回「查询」模式。")
        else:
            self._log(f"[{name}] 失败：{message}")

    def _on_poll(self, value, velocity, err):
        if err:
            self.edt_value.setText("ERR")
            self.edt_vel.setText("ERR")
            if err != self._last_poll_err:
                self._log(f"[轮询] {err}")
            self._last_poll_err = err
        else:
            self.edt_value.setText(str(value))
            self.edt_vel.setText(str(velocity))
            self._last_poll_err = ""

    def _log(self, text):
        self.txt_log.appendPlainText(
            f"[{datetime.now().strftime('%H:%M:%S')}] {text}")

    # ---------------- 退出 ----------------
    def closeEvent(self, event):
        self._timer.stop()
        self.req_close.emit()
        self._thread.quit()
        self._thread.wait(2500)
        event.accept()
