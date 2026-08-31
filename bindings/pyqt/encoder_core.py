# -*- coding: utf-8 -*-
"""ctypes 封装 encoder_core.dll —— bindings/c_api.h 的 Python 镜像。

只做参数搬砖：固定宽度类型进出，错误统一抛 CoreError。
所有调用必须发生在同一线程（工作线程），与 DLL 内部状态一致。
"""
import ctypes
from pathlib import Path

_DLL = Path(__file__).resolve().parents[2] / "bin" / "encoder_core.dll"

try:
    _lib = ctypes.CDLL(str(_DLL))
except OSError as e:
    raise SystemExit(
        f"无法加载 {_DLL}（{e}）。\n"
        "请先在 VSCode 中运行任务 `build encoder core dll` 构建上位机核心 DLL。"
    )

# ---- 错误码（与 c_api.h 对齐）----
ENC_OK = 0
ENC_ERR_INVALID_ARG = 1
ENC_ERR_TRANSPORT = 2
ENC_ERR_TIMEOUT = 3
ENC_ERR_BAD_RESPONSE = 4
ENC_ERR_REJECTED = 5
ENC_ERR_OPEN_FAILED = 6
ENC_ERR_UNKNOWN = 7

_ERROR_NAME = {
    ENC_OK: "OK",
    ENC_ERR_INVALID_ARG: "参数非法",
    ENC_ERR_TRANSPORT: "传输失败",
    ENC_ERR_TIMEOUT: "超时",
    ENC_ERR_BAD_RESPONSE: "应答校验失败",
    ENC_ERR_REJECTED: "设备拒绝",
    ENC_ERR_OPEN_FAILED: "打开失败",
    ENC_ERR_UNKNOWN: "未知错误",
}


class CoreError(Exception):
    """DLL 返回非 0 错误码时抛出。"""

    def __init__(self, code, message):
        self.code = code
        super().__init__(f"[{_ERROR_NAME.get(code, code)}] {message}")


class EncConfig(ctypes.Structure):
    _fields_ = [
        ("device_index", ctypes.c_uint32),
        ("channel_index", ctypes.c_uint32),
        ("baud_rate", ctypes.c_uint32),
        ("encoder_id", ctypes.c_uint8),
    ]


_void_p = ctypes.c_void_p
_char_p = ctypes.c_char_p
_u32 = ctypes.c_uint32
_i32 = ctypes.c_int32
_u8 = ctypes.c_uint8
_u16 = ctypes.c_uint16


def _declare(name, restype, argtypes):
    fn = getattr(_lib, name)
    fn.restype = restype
    fn.argtypes = argtypes
    return fn


_enc_open = _declare("enc_open", _void_p, [ctypes.POINTER(EncConfig)])
_enc_close = _declare("enc_close", None, [_void_p])
_enc_last_error = _declare("enc_last_error", _char_p, [])
_enc_read_value = _declare("enc_read_value", _i32, [_void_p, ctypes.POINTER(_u32)])
_enc_read_velocity = _declare("enc_read_velocity", _i32, [_void_p, ctypes.POINTER(_i32)])
_enc_set_id = _declare("enc_set_id", _i32, [_void_p, _u8])
_enc_set_baud_rate = _declare("enc_set_baud_rate", _i32, [_void_p, _u8])
_enc_set_mode = _declare("enc_set_mode", _i32, [_void_p, _u8])
_enc_set_auto_report_time = _declare("enc_set_auto_report_time", _i32, [_void_p, _u16])
_enc_set_zero = _declare("enc_set_zero", _i32, [_void_p])
_enc_set_direction = _declare("enc_set_direction", _i32, [_void_p, _u8])
_enc_set_velocity_sample_time = _declare("enc_set_velocity_sample_time", _i32, [_void_p, _u16])
_enc_set_midpoint = _declare("enc_set_midpoint", _i32, [_void_p])
_enc_set_value = _declare("enc_set_value", _i32, [_void_p, _u32])
_enc_set_five_turns = _declare("enc_set_five_turns", _i32, [_void_p])


def _last_error_text():
    msg = _enc_last_error()
    return msg.decode("utf-8", "replace") if msg else "无错误描述"


def _check(rc):
    if rc != ENC_OK:
        raise CoreError(rc, _last_error_text())


class Device:
    """encoder_core.dll 的薄封装。创建/使用/关闭必须发生在同一线程。"""

    def __init__(self, device_index=0, channel_index=1, baud_rate=500000,
                 encoder_id=1):
        self._handle = None
        cfg = EncConfig(device_index, channel_index, baud_rate, encoder_id)
        handle = _enc_open(ctypes.byref(cfg))
        if not handle:
            raise CoreError(ENC_ERR_OPEN_FAILED, _last_error_text())
        self._handle = handle

    def close(self):
        if self._handle:
            _enc_close(self._handle)
            self._handle = None

    def __del__(self):
        self.close()

    # ---- 12 条指令 ----
    def read_value(self):
        out = _u32()
        _check(_enc_read_value(self._handle, ctypes.byref(out)))
        return out.value

    def read_velocity(self):
        out = _i32()
        _check(_enc_read_velocity(self._handle, ctypes.byref(out)))
        return out.value

    def set_id(self, id_):
        _check(_enc_set_id(self._handle, id_))

    def set_baud_rate(self, rate):
        _check(_enc_set_baud_rate(self._handle, rate))

    def set_mode(self, mode):
        _check(_enc_set_mode(self._handle, mode))

    def set_auto_report_time(self, us):
        _check(_enc_set_auto_report_time(self._handle, us))

    def set_zero(self):
        _check(_enc_set_zero(self._handle))

    def set_direction(self, direction):
        _check(_enc_set_direction(self._handle, direction))

    def set_velocity_sample_time(self, ms):
        _check(_enc_set_velocity_sample_time(self._handle, ms))

    def set_midpoint(self):
        _check(_enc_set_midpoint(self._handle))

    def set_value(self, value):
        _check(_enc_set_value(self._handle, value))

    def set_five_turns(self):
        _check(_enc_set_five_turns(self._handle))
