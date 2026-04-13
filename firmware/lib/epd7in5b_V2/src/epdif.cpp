/**
 * SPI + GPIO for ESP32 driver board. No separate PWR GPIO — panel power is fixed on-board.
 */

#include "epdif.h"
#include <SPI.h>

// VSPI default SCLK=18, MISO=19, MOSI=23 on many boards — we use GPIO 13/14 per Waveshare.
static constexpr int EPD_SCK = 13;
static constexpr int EPD_MOSI = 14;

EpdIf::EpdIf() {}

EpdIf::~EpdIf() {}

void EpdIf::DigitalWrite(int pin, int value) {
    digitalWrite(pin, value);
}

int EpdIf::DigitalRead(int pin) {
    return digitalRead(pin);
}

void EpdIf::DelayMs(unsigned int delaytime) {
    delay(delaytime);
}

void EpdIf::SpiTransfer(unsigned char data) {
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(data);
    digitalWrite(CS_PIN, HIGH);
}

int EpdIf::IfInit(void) {
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    pinMode(RST_PIN, OUTPUT);
    pinMode(DC_PIN, OUTPUT);
    pinMode(BUSY_PIN, INPUT);

    SPI.begin(EPD_SCK, -1, EPD_MOSI, -1);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    return 0;
}
