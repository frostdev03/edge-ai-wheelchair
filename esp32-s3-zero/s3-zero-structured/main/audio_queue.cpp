#include "config.h"
#include "audio_queue.h"

QueueHandle_t audioQueue;
QueueHandle_t inferenceQueue;

void initQueues() {

    audioQueue = xQueueCreate(
        8,
        sizeof(AudioChunk)
    );

    inferenceQueue = xQueueCreate(
        4,
        sizeof(AudioChunk)
    );
}