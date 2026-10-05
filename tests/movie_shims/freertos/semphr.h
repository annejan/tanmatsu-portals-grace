#pragma once
// Host stand-in: the audio DSP's lock, which one thread never contends.
typedef void* SemaphoreHandle_t;
#define xSemaphoreCreateMutex() ((SemaphoreHandle_t)1)
#define xSemaphoreTake(m, t)    (1)
#define xSemaphoreGive(m)       (1)
#define portMAX_DELAY           0
