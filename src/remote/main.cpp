// Remote unit - Seeed XIAO ESP32S3 + Wio-SX1262 LoRa kit, powered over USB-C.
// No extra wiring is needed.
//
// Tells the charging station (src/station/main.cpp) how much current is
// available, and prints the station's answer.
//
// Where the "available current" comes from is up to you: by default it is
// typed on the serial monitor (e.g. "10" + Enter for 10 A, "0" to pause).
// To feed it from something else (energy meter, solar inverter, MQTT ...)
// replace the body of readAvailableCurrent().
#include <Arduino.h>
#include "xiao_sx1262.h"

// The green LED on the LoRa board is on while the station answers.

// Uncomment to set the current with a potentiometer (wiper to D0, ends to 3V3
// and GND) instead of the serial monitor. Full scale = POT_FULL_SCALE_A.
// #define SETPOINT_FROM_POT
#define PIN_POT           1   // D0
#define POT_FULL_SCALE_A  16.0f

// ------------------------------------------------------------ Globals ------
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

volatile bool radioIrq = false;  // set by DIO1 when a packet has been received
bool radioOk = false;

bool haveSetpoint = false;       // nothing is sent until a setpoint is known
float setpointAmps = 0;
float lastSentAmps = -1;
uint8_t seq = 0;
uint32_t lastSendMs = 0;
uint32_t lastAckMs = 0;
bool everAcked = false;
String serialLine;

void IRAM_ATTR onRadioIrq() {
  radioIrq = true;
}

// ------------------------------------------------------------ Setpoint -----
// Updates setpointAmps / haveSetpoint. Called on every pass of loop().
void readAvailableCurrent() {
#ifdef SETPOINT_FROM_POT
  static uint32_t lastReadMs = 0;
  if (millis() - lastReadMs < 500) return;
  lastReadMs = millis();
  float amps = analogReadMilliVolts(PIN_POT) * POT_FULL_SCALE_A / 3300.0f;
  if (amps > POT_FULL_SCALE_A) amps = POT_FULL_SCALE_A;
  amps = roundf(amps * 2.0f) / 2.0f;        // 0.5 A steps, so noise does not cause resends
  setpointAmps = amps;
  haveSetpoint = true;
#else
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialLine.trim();
      if (serialLine.length() > 0) {
        float amps = serialLine.toFloat();
        if (amps >= 0 && amps <= 80) {
          setpointAmps = amps;
          haveSetpoint = true;
          Serial.printf("Setpoint: %.1f A\n", setpointAmps);
        } else {
          Serial.println("Enter the available current in A (0 = pause)");
        }
      }
      serialLine = "";
    } else if (serialLine.length() < 16) {
      serialLine += c;
    }
  }
#endif
}

// ------------------------------------------------------------ Radio --------
void sendSetpoint() {
  LinkSetpoint msg;
  msg.magic = LINK_MAGIC;
  msg.type = LINK_TYPE_SETPOINT;
  msg.stationId = LINK_STATION_ID;
  msg.seq = ++seq;
  msg.deciAmps = (uint16_t)(setpointAmps * 10.0f + 0.5f);

  int16_t err = radio.transmit((uint8_t *)&msg, sizeof(msg));  // blocks ~40 ms at SF7
  if (err != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa transmit failed, error %d\n", err);
  }
  lastSendMs = millis();
  lastSentAmps = setpointAmps;
  radioIrq = false;        // discard the "transmission done" interrupt
  radio.startReceive();    // listen for the station's answer
}

void receiveStatus() {
  LinkStatus status;
  size_t len = radio.getPacketLength();
  int16_t err = radio.readData((uint8_t *)&status, sizeof(status));
  float rssi = radio.getRSSI();
  radio.startReceive();
  if (err != RADIOLIB_ERR_NONE || len != sizeof(status) || status.magic != LINK_MAGIC ||
      status.type != LINK_TYPE_STATUS || status.stationId != LINK_STATION_ID) {
    return;
  }
  everAcked = true;
  lastAckMs = millis();
  Serial.printf("Station: state=%c offering=%.1f A (ack %u, rssi %.0f dBm)\n", status.state,
                status.offeredDeciAmps / 10.0f, status.seq, rssi);
}

// ------------------------------------------------------------ Setup --------
void setup() {
  pinMode(PIN_WIO_LED, OUTPUT);
  digitalWrite(PIN_WIO_LED, LOW);

  Serial.begin(115200);
  Serial.println();

  int16_t err = xiaoRadioBegin(radio, onRadioIrq);
  radioOk = (err == RADIOLIB_ERR_NONE);
  if (!radioOk) {
    Serial.printf("LoRa radio not responding, error %d - is the LoRa board seated?\n", err);
  } else {
#ifndef SETPOINT_FROM_POT
    Serial.println("Type the available current in A and press Enter (0 = pause)");
#endif
  }
}

// ------------------------------------------------------------ Loop ---------
void loop() {
  readAvailableCurrent();
  if (!radioOk) return;

  if (radioIrq) {
    radioIrq = false;
    receiveStatus();
  }

  uint32_t sinceSend = millis() - lastSendMs;
  bool changed = setpointAmps != lastSentAmps;
  bool first = lastSentAmps < 0;
  if (haveSetpoint && (first || sinceSend >= LINK_SEND_INTERVAL_MS ||
                       (changed && sinceSend >= LINK_MIN_INTERVAL_MS))) {
    sendSetpoint();
  }

  bool linkOk = everAcked && millis() - lastAckMs < 3 * LINK_SEND_INTERVAL_MS;
  digitalWrite(PIN_WIO_LED, linkOk);
}
