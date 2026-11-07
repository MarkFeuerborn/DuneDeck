#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef enum { I2S_NUM_0 = 0 } i2s_port_t;
typedef enum { I2S_MODE_MASTER = 1, I2S_MODE_TX = 2 } i2s_mode_t;
typedef enum { I2S_BITS_PER_SAMPLE_16BIT = 16 } i2s_bits_per_sample_t;
typedef enum { I2S_CHANNEL_FMT_RIGHT_LEFT = 0 } i2s_channel_fmt_t;
typedef enum { I2S_COMM_FORMAT_STAND_I2S = 1 } i2s_comm_format_t;
#define ESP_INTR_FLAG_LEVEL1 2
#define I2S_PIN_NO_CHANGE (-1)
typedef struct { i2s_mode_t mode; int sample_rate; i2s_bits_per_sample_t bits_per_sample; i2s_channel_fmt_t channel_format;
  i2s_comm_format_t communication_format; int intr_alloc_flags; int dma_buf_count; int dma_buf_len; bool use_apll; } i2s_config_t;
// Field set per ESP-IDF 4.4 (what arduino-esp32 2.0.x is built on). mck_io_num is the one that bit us.
typedef struct { int mck_io_num; int bck_io_num; int ws_io_num; int data_out_num; int data_in_num; } i2s_pin_config_t;
esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t*, int, void*);
esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t*);
esp_err_t i2s_write(i2s_port_t, const void*, size_t, size_t*, uint32_t);
esp_err_t i2s_driver_uninstall(i2s_port_t);
