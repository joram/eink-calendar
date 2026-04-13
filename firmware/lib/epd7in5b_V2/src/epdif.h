/**
 * Pinout: Waveshare E-Paper ESP32 Driver Board (Rev 2.1+ / Rev 3)
 * https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board
 *
 * DIN=14 (MOSI), SCLK=13, CS=15, DC=27, RST=26, BUSY=25
 */

#ifndef EPDIF_H
#define EPDIF_H

#include <Arduino.h>

#define RST_PIN 26
#define DC_PIN 27
#define CS_PIN 15
#define BUSY_PIN 25

class EpdIf {
public:
    EpdIf(void);
    ~EpdIf(void);

    static int IfInit(void);
    static void DigitalWrite(int pin, int value);
    static int DigitalRead(int pin);
    static void DelayMs(unsigned int delaytime);
    static void SpiTransfer(unsigned char data);
};

#endif
