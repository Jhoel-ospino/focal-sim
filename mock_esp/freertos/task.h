#pragma once

#include "FreeRTOS.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

typedef int BaseType_t;
#define pdPASS 1

static inline void vTaskDelay(TickType_t xTicksToDelay) {
    usleep(xTicksToDelay * 1000);
}

static inline void vTaskDelete(TaskHandle_t xTaskToDelete) {
    (void)xTaskToDelete;
    pthread_exit(NULL);
}

static inline BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                                     const char * const pcName,
                                     const uint32_t usStackDepth,
                                     void * const pvParameters,
                                     uint32_t uxPriority,
                                     TaskHandle_t * const pxCreatedTask) {
    (void)pcName;
    (void)usStackDepth;
    (void)uxPriority;
    pthread_t thread;
    if (pthread_create(&thread, NULL, (void *(*)(void *))pxTaskCode, pvParameters) == 0) {
        pthread_detach(thread);
        if (pxCreatedTask) *pxCreatedTask = (TaskHandle_t)thread;
        return pdPASS;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
