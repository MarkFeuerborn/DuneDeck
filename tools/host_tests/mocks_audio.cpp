// Mocks for the audio tests. i2s_set_pin CAPTURES the pin struct so tests can assert what we claim.
#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <stdlib.h>
SerialStub Serial;
uint32_t millis() { return 0; } void delay(uint32_t) {}
i2s_pin_config_t g_pins; int g_pinsSet = 0; int g_installed = 0;
esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t*, int, void*) { g_installed++; return ESP_OK; }
esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t* p) { g_pins = *p; g_pinsSet++; return ESP_OK; }
esp_err_t i2s_write(i2s_port_t, const void*, size_t n, size_t* w, uint32_t) { *w = n; return ESP_OK; }
esp_err_t i2s_driver_uninstall(i2s_port_t) { return ESP_OK; }
BaseType_t xTaskCreatePinnedToCore(void(*)(void*), const char*, uint32_t, void*, int, TaskHandle_t* h, int) { if (h) *h = (void*)1; return pdPASS; }
void vTaskDelete(TaskHandle_t) {} void vTaskDelay(TickType_t) {}
SemaphoreHandle_t xSemaphoreCreateMutex() { return (void*)1; } int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return 1; }
int xSemaphoreGive(SemaphoreHandle_t) { return 1; } void vSemaphoreDelete(SemaphoreHandle_t) {}
void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); } void heap_caps_free(void* p) { free(p); }
void SerialStub::printf(const char*, ...) {} void SerialStub::println(const char*) {}
