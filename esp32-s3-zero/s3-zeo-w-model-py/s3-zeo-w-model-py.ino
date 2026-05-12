#include <Arduino.h>
#include "TFLiteMicro_ArduinoESP32S3.h"
#include "voice_model.h"
#include "driver/i2s_std.h"

// --- KONFIGURASI PINOUT S3 ZERO ---
#define I2S_WS   9
#define I2S_SCK  8
#define I2S_SD   7

// --- PARAMETER MODEL ---
#define ARENA_SIZE 60000 
#define NUM_OPS    10   // Kita batasi 10 operator saja supaya hemat RAM

i2s_chan_handle_t rx_chan;

// FIX: Fungsi Resolver Manual untuk memenuhi syarat library
tflite::MicroMutableOpResolver<NUM_OPS> MyResolver() {
    tflite::MicroMutableOpResolver<NUM_OPS> res;
    // Tambahkan operator yang biasanya ada di model CNN/Voice
    res.AddConv2D();
    res.AddDepthwiseConv2D();
    res.AddFullyConnected();
    res.AddSoftmax();
    res.AddReshape();
    res.AddAveragePool2D();
    res.AddMaxPool2D();
    res.AddQuantize();
    res.AddDequantize();
    return res;
}

void setup_i2s() {
    // FIX: Menggunakan I2S_CHANNEL_DEFAULT_CONFIG untuk ESP32 Core 3.x
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, NULL, &rx_chan);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SCK,
            .ws   = (gpio_num_t)I2S_WS,
            .din  = (gpio_num_t)I2S_SD,
        }
    };
    i2s_channel_init_std_mode(rx_chan, &std_cfg);
    i2s_channel_enable(rx_chan);
}

void setup() {
    Serial.begin(115200);
    while(!Serial);

    Serial.println("--- Proyek Kursi Roda UNY - Fahril ---");

    // FIX: Memanggil TFLMsetupModel dengan fungsi resolver yang benar
    // Kita panggil MyResolver sebagai argumen kedua
    TFLMinterpreter = TFLMsetupModel<NUM_OPS, ARENA_SIZE>(voice_model, MyResolver);

    if (!TFLMinterpreter) {
        Serial.println("Gagal memuat model! Periksa kecocokan NUM_OPS atau ARENA_SIZE.");
        while(1);
    }

    setup_i2s();
    Serial.println("Sistem Siap! Mendengarkan...");
}

void loop() {
    int32_t raw_samples[512];
    size_t bytes_read = 0;

    if (i2s_channel_read(rx_chan, &raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
        // Asumsi model manual Python menggunakan tipe float32
        float* input_data = tflite::GetTensorData<float>(TFLMinput);
        
        // Normalisasi audio (contoh sederhana)
        for (int i = 0; i < TFLMinput->dims->data[1]; i++) {
            input_data[i] = (float)raw_samples[i % (bytes_read/4)] / 2147483648.0f;
        }

        TFLMpredict();

        // Ambil hasil prediksi
        float* output_data = tflite::GetTensorData<float>(TFLMoutput);
        float max_val = 0;
        int label = -1;
        
        for (int i = 0; i < TFLMoutput->dims->data[1]; i++) {
            if (output_data[i] > max_val) {
                max_val = output_data[i];
                label = i;
            }
        }

        if (max_val > 0.85) {
            Serial.printf("Command: %d | Confidence: %.2f\n", label, max_val);
        }
    }
}