// Seeed "XIAO ESP32S3 & Wio-SX1262" kit: how the LoRa board is wired to the
// ESP32-S3, and the radio start-up sequence. Used by both station and remote.
//
// The LoRa board plugs onto the board-to-board connector on top of the XIAO.
// Its control pins are not on the XIAO headers; only the SPI bus is shared
// with header pins D8, D9 and D10, which must therefore stay unconnected.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "evse_link.h"

#define PIN_LORA_SCK    7   // also header pin D8
#define PIN_LORA_MISO   8   // also header pin D9
#define PIN_LORA_MOSI   9   // also header pin D10
#define PIN_LORA_RF_SW  38  // antenna switch, HIGH = receive
#define PIN_LORA_DIO1   39  // interrupt: packet received / sent
#define PIN_LORA_BUSY   40
#define PIN_LORA_NSS    41
#define PIN_LORA_RST    42
#define PIN_WIO_LED     48  // green LED on the LoRa board, active high
#define PIN_WIO_BUTTON  21  // button on the LoRa board, pressed = LOW

// The module's crystal oscillator (TCXO) is powered from the SX1262's DIO3 pin.
#define LORA_TCXO_VOLT  1.8

// Starts the radio with the settings from evse_link.h and leaves it listening.
// Returns RADIOLIB_ERR_NONE (0) on success.
inline int16_t xiaoRadioBegin(SX1262 &radio, void (*onIrq)(void)) {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  int16_t err = radio.begin(LINK_FREQUENCY_MHZ, LINK_BANDWIDTH_KHZ, LINK_SPREADING,
                            LINK_CODING_RATE, LINK_SYNC_WORD, LINK_TX_POWER_DBM,
                            LINK_PREAMBLE, LORA_TCXO_VOLT);
  if (err != RADIOLIB_ERR_NONE) return err;
  radio.setRfSwitchPins(PIN_LORA_RF_SW, RADIOLIB_NC);
  err = radio.setDio2AsRfSwitch(true);  // DIO2 switches the antenna to transmit
  if (err != RADIOLIB_ERR_NONE) return err;
  err = radio.setCRC(2);
  if (err != RADIOLIB_ERR_NONE) return err;
  radio.setDio1Action(onIrq);
  return radio.startReceive();
}
