#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <Arduino.h>

void initI2S();
void audioTask(void *pvParameters);

#endif