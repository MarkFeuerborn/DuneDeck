#pragma once
#include <stdint.h>
typedef void* TaskHandle_t; typedef void* SemaphoreHandle_t; typedef int BaseType_t; typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
