#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include <edge-impulse-sdk/classifier/ei_run_classifier.h>

void processCommand(ei_impulse_result_t &result);

#endif