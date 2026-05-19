#include "CompletelyoWorksKeywordsSpotting_inferencing.h"
//#include "SmartWheelchair-Final_inferencing.h"
//#include "SmartWheelchairWithKeywordSpotting_inferencing.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"

#define I2S_WS   9
#define I2S_SCK  8
#define I2S_SD   7

#define THRESHOLD_KEYWORD 0.70f

#define AUDIO_SLICE_SIZE EI_CLASSIFIER_SLICE_SIZE
#define SAMPLE_RATE 16000

#define I2S_READ_LEN 512
#define AUDIO_QUEUE_LENGTH 8

#define DEBUG_PROBABILITAS true

// =====================================================
// GLOBAL
// =====================================================

i2s_chan_handle_t rx_chan;

QueueHandle_t audioQueue;
SemaphoreHandle_t inferenceMutex;

static bool inferenceRunning = false;
static bool audioReady = false;

typedef struct {
  int16_t samples[I2S_READ_LEN];
  size_t sampleCount;
} audio_chunk_t;

static int16_t inferenceBuffer[AUDIO_SLICE_SIZE];
static volatile uint32_t inferenceIndex = 0;

// =====================================================
// I2S
// =====================================================

void setupI2S() {

  i2s_chan_config_t rx_chan_cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

  i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

  i2s_std_config_t rx_std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT,
      I2S_SLOT_MODE_MONO
    ),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK,
      .ws = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)I2S_SD,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };

  rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
  i2s_channel_enable(rx_chan);
}

// =====================================================
// EDGE IMPULSE CALLBACK
// =====================================================

static int microphone_audio_signal_get_data(
  size_t offset,
  size_t length,
  float *out_ptr
) {
  numpy::int16_to_float(&inferenceBuffer[offset], out_ptr, length);
  return 0;
}

// =====================================================
// AUDIO TASK (PRODUCER)
// =====================================================

void audioTask(void *parameter) {

  int32_t rawSamples[I2S_READ_LEN];
  size_t bytesRead = 0;

  audio_chunk_t chunk;

  while (true) {

    esp_err_t result = i2s_channel_read(
                         rx_chan,
                         rawSamples,
                         sizeof(rawSamples),
                         &bytesRead,
                         portMAX_DELAY
                       );

    if (result == ESP_OK) {

      chunk.sampleCount = bytesRead / 4;

      for (int i = 0; i < chunk.sampleCount; i++) {
        chunk.samples[i] = (int16_t)(rawSamples[i] >> 14);
      }

      xQueueSend(audioQueue, &chunk, portMAX_DELAY);
    }
  }
}

// =====================================================
// INFERENCE TASK (CONSUMER)
// =====================================================

void inferenceTask(void *parameter) {

  audio_chunk_t receivedChunk;

  signal_t signal;
  signal.total_length = AUDIO_SLICE_SIZE;
  signal.get_data = &microphone_audio_signal_get_data;

  while (true) {

    if (xQueueReceive(audioQueue, &receivedChunk, portMAX_DELAY)) {

      for (int i = 0; i < receivedChunk.sampleCount; i++) {

        inferenceBuffer[inferenceIndex++] = receivedChunk.samples[i];

        if (inferenceIndex >= AUDIO_SLICE_SIZE) {

          inferenceIndex = 0;
          audioReady = true;
        }
      }

      if (audioReady && !inferenceRunning) {

        inferenceRunning = true;
        audioReady = false;

        xSemaphoreTake(inferenceMutex, portMAX_DELAY);

        ei_impulse_result_t result = { 0 };

        EI_IMPULSE_ERROR res = run_classifier_continuous(
                                 &signal,
                                 &result,
                                 false
                               );

        if (res == EI_IMPULSE_OK) {

          float highestValue = 0.0f;
          const char *highestLabel = "unknown";

          for (size_t ix = 0;
               ix < EI_CLASSIFIER_LABEL_COUNT;
               ix++) {

            if (DEBUG_PROBABILITAS) {
              ei_printf("%s: %.2f\n",
                        result.classification[ix].label,
                        result.classification[ix].value);
            }

            if (result.classification[ix].value > highestValue) {
              highestValue = result.classification[ix].value;
              highestLabel = result.classification[ix].label;
            }
          }

          if (highestValue >= THRESHOLD_KEYWORD) {
            ei_printf(">>> DETECTED: %s (%.2f)\n",
                      highestLabel,
                      highestValue);
          }
        }

        xSemaphoreGive(inferenceMutex);

        inferenceRunning = false;
      }
    }
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(2000);

  ei_printf("RTOS Keyword Spotting Start\n");

  run_classifier_init();

  setupI2S();

  audioQueue = xQueueCreate(
                 AUDIO_QUEUE_LENGTH,
                 sizeof(audio_chunk_t)
               );

  inferenceMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    audioTask,
    "Audio Task",
    8192,
    NULL,
    3,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    inferenceTask,
    "Inference Task",
    16384,
    NULL,
    2,
    NULL,
    1
  );
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
