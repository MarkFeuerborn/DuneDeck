#pragma once
#include "FreeRTOS.h"
SemaphoreHandle_t xSemaphoreCreateMutex(); int xSemaphoreTake(SemaphoreHandle_t, TickType_t); int xSemaphoreGive(SemaphoreHandle_t); void vSemaphoreDelete(SemaphoreHandle_t);
