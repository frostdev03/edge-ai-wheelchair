#include "config.h"
#include <TensorFlowLite_ESP32.h>
#include <edge-impulse-sdk/classifier/ei_run_classifier.h>

static int16_t ringBuffer[RING_BUFFER_SAMPLES];
static size_t writeIndex = 0;

static int microphone_audio_signal_get_data(
    size_t offset,
    size_t length,
    float *out_ptr
) {

    numpy::int16_to_float(
        &ringBuffer[offset],
        out_ptr,
        length
    );

    return 0;
}

void inferenceTask(void *pvParameters) {

    AudioChunk chunk;

    while (true) {

        if (xQueueReceive(audioQueue, &chunk, portMAX_DELAY)) {

            xEventGroupSetBits(systemEvents, EVT_INFERENCE_BUSY);

            memmove(
                ringBuffer,
                ringBuffer + STRIDE_SAMPLES,
                (WINDOW_SAMPLES - STRIDE_SAMPLES) * sizeof(int16_t)
            );

            memcpy(
                ringBuffer + (WINDOW_SAMPLES - STRIDE_SAMPLES),
                chunk.samples,
                STRIDE_SAMPLES * sizeof(int16_t)
            );

            signal_t signal;
            signal.total_length = WINDOW_SAMPLES;
            signal.get_data = microphone_audio_signal_get_data;

            ei_impulse_result_t result = {0};

            EI_IMPULSE_ERROR res = run_classifier(
                &signal,
                &result,
                false
            );

            if (res == EI_IMPULSE_OK) {
                processCommand(result);
            }

            xEventGroupClearBits(systemEvents, EVT_INFERENCE_BUSY);
        }
    }
}