#include "config.h"
#include "command_handler.h"

static int kananCount  = 0;
static int kiriCount   = 0;
static int majuCount   = 0;
static int mundurCount = 0;
static int stopCount   = 0;

void processCommand(ei_impulse_result_t &result) {

    float kanan  = result.classification[0].value;
    float kiri   = result.classification[1].value;
    float maju   = result.classification[2].value;
    float mundur = result.classification[3].value;
    float stop   = result.classification[4].value;

    Serial.printf(
        "kanan=%.2f kiri=%.2f maju=%.2f mundur=%.2f stop=%.2f\n",
        kanan,
        kiri,
        maju,
        mundur,
        stop
    );

    kananCount  = (kanan  > COMMAND_THRESHOLD) ? kananCount + 1 : 0;
    kiriCount   = (kiri   > COMMAND_THRESHOLD) ? kiriCount + 1 : 0;
    majuCount   = (maju   > COMMAND_THRESHOLD) ? majuCount + 1 : 0;
    mundurCount = (mundur > COMMAND_THRESHOLD) ? mundurCount + 1 : 0;
    stopCount   = (stop   > COMMAND_THRESHOLD) ? stopCount + 1 : 0;

    if (kananCount >= COMMAND_CONFIRM_COUNT) {
        Serial.println("COMMAND: KANAN");
        kananCount = 0;
    }

    if (kiriCount >= COMMAND_CONFIRM_COUNT) {
        Serial.println("COMMAND: KIRI");
        kiriCount = 0;
    }

    if (majuCount >= COMMAND_CONFIRM_COUNT) {
        Serial.println("COMMAND: MAJU");
        majuCount = 0;
    }

    if (mundurCount >= COMMAND_CONFIRM_COUNT) {
        Serial.println("COMMAND: MUNDUR");
        mundurCount = 0;
    }

    if (stopCount >= COMMAND_CONFIRM_COUNT) {
        Serial.println("COMMAND: STOP");
        stopCount = 0;
    }
}