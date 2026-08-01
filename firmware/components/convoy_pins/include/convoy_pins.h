/**
 * convoy_pins — the ESP32-S3-DevKitC-1 (N16R8) GPIO map, transcribed
 * one-to-one from the pin table in `docs/02-hardware.md` (which is the
 * single source of truth). Header-only.
 *
 * **Never hard-code a GPIO number anywhere else.** If a pin moves, it
 * moves in docs/02 first and then here, in the same commit.
 *
 * Every net below is a general-purpose bidirectional S3 GPIO with an
 * internal pull-up available, so buttons need no external resistors.
 */
#ifndef CONVOY_PINS_H
#define CONVOY_PINS_H

/* ---- ILI9341 display (SPI2/FSPI) ------------------------------------ */
#define CONVOY_PIN_TFT_SCK 12  /* 40 MHz                                 */
#define CONVOY_PIN_TFT_MOSI 11
#define CONVOY_PIN_TFT_MISO 13 /* optional                               */
#define CONVOY_PIN_TFT_CS 10
#define CONVOY_PIN_TFT_DC 14
#define CONVOY_PIN_TFT_RST 9
#define CONVOY_PIN_TFT_BL 21 /* backlight, LEDC PWM                      */

/* ---- SX1262 LoRa radio, E22-900M22S (SPI3) -------------------------- */
#define CONVOY_PIN_LORA_SCK 5 /* <=10 MHz                                */
#define CONVOY_PIN_LORA_MOSI 6
#define CONVOY_PIN_LORA_MISO 4
#define CONVOY_PIN_LORA_NSS 7
#define CONVOY_PIN_LORA_RST 15
#define CONVOY_PIN_LORA_BUSY 16
#define CONVOY_PIN_LORA_DIO1 17 /* IRQ: RX-done / TX-done               */
#define CONVOY_PIN_LORA_TXEN 18 /* E22 RF switch                        */
#define CONVOY_PIN_LORA_RXEN 8  /* E22 RF switch                        */

/* ---- GY-NEO6MV2 GPS (UART1) ----------------------------------------- */
#define CONVOY_PIN_GPS_RX 38 /* NEO-6M TX -> ESP32                       */
#define CONVOY_PIN_GPS_TX 39 /* NEO-6M RX <- ESP32, UBX config only      */

/* ---- I2S audio: INMP441 mic + MAX98357A amp (shared clocks) --------- */
#define CONVOY_PIN_I2S_BCLK 40 /* INMP441 SCK + MAX98357A BCLK          */
#define CONVOY_PIN_I2S_WS 41   /* INMP441 WS  + MAX98357A LRC           */
#define CONVOY_PIN_I2S_DIN 42  /* INMP441 SD -> ESP32 (mic in)          */
#define CONVOY_PIN_I2S_DOUT 47 /* ESP32 -> MAX98357A DIN (speaker out)  */

/* ---- Buttons (to GND, internal pull-up, active low) ----------------- */
#define CONVOY_PIN_BTN_PTT 1
#define CONVOY_PIN_BTN_AUX 2 /* zoom / backlight                        */

/* ---- Status ---------------------------------------------------------- */
#define CONVOY_PIN_STATUS_LED 48 /* onboard WS2812 RGB, optional        */

/*
 * Reserved — never assign a peripheral to these (docs/02 §Reserved):
 *   26-32   in-package SPI flash (all S3 modules)
 *   33-37   in-package octal PSRAM (N16R8)
 *   19, 20  native USB D-/D+ (USB-JTAG/serial console)
 *   43, 44  UART0 TXD0/RXD0 (the other console route)
 *   0       BOOT strapping button (keep free for flashing)
 *   3,45,46 strapping pins (leave unconnected)
 */

#endif /* CONVOY_PINS_H */
