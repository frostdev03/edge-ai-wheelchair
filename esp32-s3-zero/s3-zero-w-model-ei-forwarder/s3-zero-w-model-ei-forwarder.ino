#include <Arduino.h>
#include "driver/i2s_std.h"

/*biar 16 bit
  defaultnya float (32 bit)*/
#define EIDSP_QUANTIZE_FILTERBANK   0

//my library ai gweh
#include <SmartWheelchair_inferencing.h>

// pin fisik esp32 s3 zero
#define I2S_WS     9
#define I2S_SCK    8
#define I2S_SD     7

#define SAMPLE_RATE   EI_CLASSIFIER_FREQUENCY // nilai frekuensi dari model (16KHz)
#define WIN_SAMPLES   EI_CLASSIFIER_RAW_SAMPLE_COUNT // total sampel dalam 1x inferensi = 16KHz * panjang window
#define STR_SAMPLES   (EI_CLASSIFIER_RAW_SAMPLE_COUNT / 2) // overlap 50% pada window

#define GAIN_FACTOR   4 // pengali biar amplitudo naik

#define CHUNK_SAMPLES 512 // panjang data yang diambil i2s per pengambilan

static int16_t *ringBuffer      = nullptr; // alamat memosri yg nyimpen arus audio yg diambil dari i2s, sizenya 2x win sample
static int16_t *inferenceBuffer = nullptr; // alamat memori yg nampung sepaket audio (1 window) yg akan dibaca model

static volatile uint32_t writeIndex = 0; // nyimpen tail atau index terakhir di ringBuffer

SemaphoreHandle_t ringMutex; // handler biar tidak ada race condition, analogi penulis papan tulis dan pembaca papan tulis
i2s_chan_handle_t rx_chan; // akses i2s

// cek QSPI RAM aktif atau g
static bool checkPSRAM()
{
  size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  size_t psramFree  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t sramFree   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

  Serial.printf("[MEM] PSRAM total : %u KB\n", psramTotal / 1024);
  Serial.printf("[MEM] PSRAM free  : %u KB\n", psramFree  / 1024);
  Serial.printf("[MEM] SRAM  free  : %u KB\n", sramFree   / 1024);

  if (psramTotal == 0) {
    Serial.println("[ERR] PSRAM tidak terdeteksi!");
    Serial.println("      Pastikan: Tools → PSRAM → QSPI PSRAM");
    return false;
  }
  return true;
}

// kyk konversi, ambil data dari inferenceBuffer (int16_t) lalu diubah jadi float/desimal biar bisa masuk layer cnn
static int microphone_audio_signal_get_data(
  size_t offset, size_t length, float *out_ptr)
{
  if ((offset + length) > (size_t)WIN_SAMPLES) return -1;

  numpy::int16_to_float(
    inferenceBuffer + offset,
    out_ptr,
    length
  );
  return 0;
}

// konfigurasi i2s dengan freq 16KHz
void setupI2S()
{
  i2s_chan_config_t rx_cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&rx_cfg, NULL, &rx_chan));

  i2s_std_config_t rx_std = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT,
      I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK, // pin fisik
      .ws   = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)I2S_SD,
      .invert_flags = { false, false, false },
    },
  };
  rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // channel kiri

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

/*prio HIGH
 * baca i2s terus menerus, per ambil sejumlah 512 sampel
 * data digeser ke kanan dengan >>14 untuk membuang bit kosong dan dikali gain factor

*/
void TaskAudioCapture(void *pvParameters)
{
  int32_t raw[CHUNK_SAMPLES];

  while (1) {
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(
                      rx_chan, raw, sizeof(raw),
                      &bytes_read, pdMS_TO_TICKS(100)
                    );

    if (ret == ESP_ERR_TIMEOUT) continue;
    if (ret != ESP_OK || bytes_read == 0) {
      Serial.printf("[ERR] I2S read: %d\n", ret);
      continue;
    }

    int n = bytes_read / 4;

    if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(10))) { 
      for (int i = 0; i < n; i++) {
        int32_t s = (int32_t)(raw[i] >> 14) * GAIN_FACTOR;
        if (s >  32767) s =  32767; // clipping biar ga over dan lower
        if (s < -32768) s = -32768;

        ringBuffer[writeIndex] = (int16_t)s;
        if (++writeIndex >= (uint32_t)(WIN_SAMPLES * 2))
          writeIndex = 0;
      }
      xSemaphoreGive(ringMutex); // lanjutkan task
    }
  }
}

// merapikan ringBuffer jadi inferenceBuffer agar urut dan rapi
void copyLatestWindow() {
  if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50))) {
    int32_t start = (int32_t)writeIndex - WIN_SAMPLES;
    if (start < 0) start += WIN_SAMPLES * 2;

    if (start + WIN_SAMPLES <= WIN_SAMPLES * 2) {
      memcpy(inferenceBuffer, ringBuffer + start, WIN_SAMPLES * sizeof(int16_t));
    } else {
      size_t firstPart = (WIN_SAMPLES * 2) - start;
      size_t secondPart = WIN_SAMPLES - firstPart;
      memcpy(inferenceBuffer, ringBuffer + start, firstPart * sizeof(int16_t));
      memcpy(inferenceBuffer + firstPart, ringBuffer, secondPart * sizeof(int16_t));
    }
    xSemaphoreGive(ringMutex);
  }
}

/* menunggu ringBuffer sampai full 1 window/16KHz yang kemudian dicopy dengan fungsi copy latest window
 * VAD memeriksa volume audio yg dibaca i2s
 *  
*/
void TaskInference(void *pvParameters)
{
  uint32_t waitMs = (uint32_t)WIN_SAMPLES * 1000 / SAMPLE_RATE + 200;
  vTaskDelay(pdMS_TO_TICKS(waitMs));

  while (1) {

    copyLatestWindow();

    int32_t sumAbs = 0;
    int16_t peakAbs = 0;
    for (int i = 0; i < WIN_SAMPLES; i++) {
      int16_t a = abs(inferenceBuffer[i]);
      sumAbs += a;
      if (a > peakAbs) peakAbs = a;
    }
    int32_t avgAbs = sumAbs / WIN_SAMPLES;

    Serial.printf("\n[AMP] avg=%ld  peak=%d\n", avgAbs, peakAbs);

    if (avgAbs < 1000) { //VAD
      Serial.println("[VAD] silence — skip");
      vTaskDelay(pdMS_TO_TICKS(STR_SAMPLES * 1000 / SAMPLE_RATE));
      continue;
    }

    // melewati threshold VAD
    signal_t signal;
    signal.total_length = WIN_SAMPLES;
    signal.get_data     = &microphone_audio_signal_get_data;

    ei_impulse_result_t result = { 0 };
    uint32_t t0 = millis();
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false); // classifier dilakukan
    uint32_t t1 = millis();

    if (r != EI_IMPULSE_OK) {
      Serial.printf("[ERR] Classifier: %d\n", r);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    
    // cetak conf score 
    Serial.printf("[TIME] %lu ms\n", (unsigned long)(t1 - t0));
    Serial.println("=== Classification ===");
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      Serial.printf("  %-8s : %.4f\n",
                    result.classification[ix].label,
                    result.classification[ix].value);
    }

    vTaskDelay(pdMS_TO_TICKS(STR_SAMPLES * 1000 / SAMPLE_RATE));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n=================================");
  Serial.println("Smart Wheelchair — Voice Detect");
  Serial.printf ("Model  : %s\n", EI_CLASSIFIER_PROJECT_NAME);
  Serial.printf ("Window : %d ms  (%d samples)\n",
                 WIN_SAMPLES * 1000 / SAMPLE_RATE, WIN_SAMPLES);
  Serial.printf ("Stride : %d ms  (%d samples)\n",
                 STR_SAMPLES * 1000 / SAMPLE_RATE, STR_SAMPLES);
  Serial.printf ("Freq   : %d Hz\n", SAMPLE_RATE);
  Serial.printf ("Gain   : x%d\n",  GAIN_FACTOR);
  Serial.println("=================================\n");

  if (!checkPSRAM()) { //cek psram
    while (1) delay(1000);
  }

  //mengalokasikan sejumlah memori untuk ngisi ini, biar ga dipake variabel atau fungsi lain
  ringBuffer = (int16_t*)heap_caps_malloc(
                 WIN_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  inferenceBuffer = (int16_t*)heap_caps_malloc(
                      WIN_SAMPLES     * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  if (!ringBuffer || !inferenceBuffer) {
    Serial.println("[ERR] Gagal alokasi buffer di PSRAM!");
    while (1) delay(1000);
  }

  memset(ringBuffer,      0, WIN_SAMPLES * 2 * sizeof(int16_t));
  memset(inferenceBuffer, 0, WIN_SAMPLES     * sizeof(int16_t));

  Serial.printf("[MEM] ringBuffer      : %u KB → PSRAM\n",
                WIN_SAMPLES * 2 * sizeof(int16_t) / 1024);
  Serial.printf("[MEM] inferenceBuffer : %u KB → PSRAM\n",
                WIN_SAMPLES     * sizeof(int16_t) / 1024);
  Serial.printf("[MEM] SRAM free (EI)  : %u KB\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);

  setupI2S();

  ringMutex = xSemaphoreCreateMutex();
  if (!ringMutex) {
    Serial.println("[ERR] Mutex creation failed");
    while (1) delay(1000);
  }

  xTaskCreatePinnedToCore(
    TaskAudioCapture, "AudioCapture",
    8192, NULL, 3, NULL, 0);

  xTaskCreatePinnedToCore(
    TaskInference, "Inference",
    24576, NULL, 1, NULL, 1);

  Serial.println("[OK] System Ready\n");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000)); //sleep
}
