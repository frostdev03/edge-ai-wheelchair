#ifndef STATE_FLAGS_H
#define STATE_FLAGS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

extern EventGroupHandle_t systemEvents;

#define EVT_AUDIO_READY      (1 << 0)
#define EVT_INFERENCE_READY  (1 << 1)
#define EVT_RECORDING        (1 << 2)
#define EVT_INFERENCE_BUSY   (1 << 3)

#endif