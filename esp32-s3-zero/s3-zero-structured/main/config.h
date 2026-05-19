#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================
// AUDIO CONFIG
// =========================
#define SAMPLE_RATE            16000
#define AUDIO_CHANNELS         1
#define SAMPLE_BITS            16

#define WINDOW_SIZE_MS         1000
#define STRIDE_SIZE_MS         200

#define WINDOW_SAMPLES         ((SAMPLE_RATE * WINDOW_SIZE_MS) / 1000)
#define STRIDE_SAMPLES         ((SAMPLE_RATE * STRIDE_SIZE_MS) / 1000)

#define RING_BUFFER_SAMPLES    WINDOW_SAMPLES
#define CHUNK_SAMPLES          STRIDE_SAMPLES

// =========================
// I2S PINS
// =========================
#define I2S_WS                 42
#define I2S_SD                 41
#define I2S_SCK                40

// =========================
// TASK CONFIG
// =========================
#define AUDIO_TASK_STACK       8192
#define INFERENCE_TASK_STACK   16384
#define COMMAND_TASK_STACK     4096

#define AUDIO_TASK_PRIORITY    3
#define INFERENCE_PRIORITY     2
#define COMMAND_PRIORITY       1

// =========================
// THRESHOLD
// =========================
#define COMMAND_THRESHOLD      0.80f
#define COMMAND_CONFIRM_COUNT  2

#endif