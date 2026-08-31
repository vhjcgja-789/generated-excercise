#ifndef ENCODER_DEMO_C_API_H
#define ENCODER_DEMO_C_API_H

/* encoder_demo 上位机 C ABI
 *
 * 跨 DLL 边界只传固定宽度 POD、不透明句柄、整数错误码，
 * 不传 std::string / 异常 / STL 容器（架构说明见工程 README「项目架构」）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#  ifdef ENC_BUILD_DLL
#    define ENC_API __declspec(dllexport)
#  else
#    define ENC_API __declspec(dllimport)
#  endif
#else
#  define ENC_API
#endif

/* 错误码：0 = 成功，其余与 enc::EncoderErrorCode 一一对应 */
#define ENC_OK               0
#define ENC_ERR_INVALID_ARG  1   /* 参数非法（发送前失败） */
#define ENC_ERR_TRANSPORT    2   /* CAN 层发送/接收失败 */
#define ENC_ERR_TIMEOUT      3   /* 超时未收到合法应答 */
#define ENC_ERR_BAD_RESPONSE 4   /* 应答帧校验不过 */
#define ENC_ERR_REJECTED     5   /* 设备拒绝（状态码非 0） */
#define ENC_ERR_OPEN_FAILED  6   /* 打开设备/通道失败 */
#define ENC_ERR_UNKNOWN      7   /* 其他内部错误 */

typedef struct {
    uint32_t device_index;   /* USBCAN-II 设备索引，通常 0 */
    uint32_t channel_index;  /* 通道号，编码器接通道 1 */
    uint32_t baud_rate;      /* 总线波特率，如 500000 */
    uint8_t  encoder_id;     /* 编码器当前 CAN 地址（1~255） */
} enc_config_t;

/* 生命周期 */
ENC_API void*       enc_open(const enc_config_t* cfg);  /* 失败返回 NULL */
ENC_API void        enc_close(void* h);                 /* NULL 安全 */

/* 本线程最近一次错误文本（UTF-8）。指针在下次调用前有效，
 * 调用方（Python ctypes）应立即拷贝。 */
ENC_API const char* enc_last_error(void);

/* 12 条编码器指令（与 kCommandTable 一致） */
ENC_API int32_t enc_read_value(void* h, uint32_t* out);               /* 0x01 */
ENC_API int32_t enc_set_id(void* h, uint8_t id);                      /* 0x02 1~255 */
ENC_API int32_t enc_set_baud_rate(void* h, uint8_t rate);             /* 0x03 0~4 */
ENC_API int32_t enc_set_mode(void* h, uint8_t mode);                  /* 0x04 */
ENC_API int32_t enc_set_auto_report_time(void* h, uint16_t us);       /* 0x05 50~65535 */
ENC_API int32_t enc_set_zero(void* h);                                /* 0x06 */
ENC_API int32_t enc_set_direction(void* h, uint8_t direction);        /* 0x07 0/1 */
ENC_API int32_t enc_read_velocity(void* h, int32_t* out);             /* 0x0A */
ENC_API int32_t enc_set_velocity_sample_time(void* h, uint16_t ms);   /* 0x0B 0~65535 */
ENC_API int32_t enc_set_midpoint(void* h);                            /* 0x0C */
ENC_API int32_t enc_set_value(void* h, uint32_t value);               /* 0x0D */
ENC_API int32_t enc_set_five_turns(void* h);                          /* 0x0F */

#ifdef __cplusplus
}
#endif

#endif /* ENCODER_DEMO_C_API_H */
