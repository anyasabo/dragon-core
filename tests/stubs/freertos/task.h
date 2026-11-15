#pragma once

#include "FreeRTOS.h"

static inline TickType_t xTaskGetTickCount(void)
{
    return 0;
}

typedef void *TaskHandle_t;

/* Defined by a host test that compiles dc_evlog.c with DC_EVLOG_LINE_ASSEMBLY=1. */
TaskHandle_t xTaskGetCurrentTaskHandle(void);
