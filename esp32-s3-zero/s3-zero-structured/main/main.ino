#include "config.h"
#include "state_flags.h"
#include "audio_queue.h"
#include "audio_driver.h"
#include "inference_engine.h"

EventGroupHandle_t systemEvents;

void setup() {

    Serial.begin(115200);
    delay(2000);

    systemEvents = xEventGroupCreate();

    initQueues();
    initI2S();

    xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        AUDIO_TASK_STACK,
        NULL,
        AUDIO_TASK_PRIORITY,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        inferenceTask,
        "InferenceTask",
        INFERENCE_TASK_STACK,
        NULL,
        INFERENCE_PRIORITY,
        NULL,
        1
    );

    Serial.println("RTOS KWS READY");
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}