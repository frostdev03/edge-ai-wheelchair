#ifndef AUDIO_QUEUE_H
#define AUDIO_QUEUE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

extern QueueHandle_t audioQueue;
extern QueueHandle_t inferenceQueue;

struct AudioChunk {
    int16_t samples[CHUNK_SAMPLES];
};

void initQueues();

#endif