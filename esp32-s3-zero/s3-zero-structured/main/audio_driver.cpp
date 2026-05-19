#include "config.h"
#include "audio_driver.h"
#include "audio_queue.h"
#include "state_flags.h"

#include <driver/i2s.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;

void initI2S() {

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

void audioTask(void *pvParameters) {

    AudioChunk chunk;
    size_t bytesRead;

    while (true) {

        i2s_read(
            I2S_PORT,
            chunk.samples,
            sizeof(chunk.samples),
            &bytesRead,
            portMAX_DELAY
        );

        BaseType_t ok = xQueueSend(
            audioQueue,
            &chunk,
            0
        );

        if (ok == pdPASS) {
            xEventGroupSetBits(systemEvents, EVT_AUDIO_READY);
        }
    }
}