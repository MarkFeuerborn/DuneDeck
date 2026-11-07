#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreatePinnedToCore(void(*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int);
void vTaskDelete(TaskHandle_t); void vTaskDelay(TickType_t);
