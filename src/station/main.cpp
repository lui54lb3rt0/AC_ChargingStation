// EV AC charging station - Seeed XIAO ESP32S3 + Wio-SX1262 LoRa kit.
//
// Same behaviour as the Arduino Uno version (src/uno/main.cpp), except that the
// current advertised to the car is no longer fixed: it is received over LoRa
// from the remote unit (src/remote/main.cpp).
//
// Educational project - see the warning in README.md before using it on mains.
#include <Arduino.h>
#include "xiao_sx1262.h"

// ------------------------------------------------------------ Pin map ------
// XIAO header pins. D8, D9, D10 belong to the LoRa board (see xiao_sx1262.h).
#define PIN_CP_IN       1   // D0, ADC - Control Pilot voltage, through the R6/R7/R8 divider
#define PIN_CP_OUT      2   // D1 - 1 kHz PWM to the comparator that drives the Control Pilot
//                      3   // D2 - strapping pin of the ESP32-S3: leave unconnected
#define PIN_RELAY       4   // D3 - HIGH = relay closed (drives the external contactor)
#define PIN_LED_CAR     5   // D4 - LED D1: car connected
#define PIN_LED_CHARGE  6   // D5 - LED D2: blinks while charging
#define PIN_LED_LINK    43  // D6 - LED D4: LoRa link ok (flickers at reset: boot log pin)
//                      44  // D7 - free
// LED D3 (power) is wired straight to 3V3 and needs no pin.

// ------------------------------------------------------------ Settings -----
// Hard limit of THIS installation (cable, contactor, breaker). The remote can
// never raise the current above it. Do not set it higher than your wiring allows.
#define MAX_CURRENT_A        16.0f
// Lowest current the standard (IEC 61851) allows to advertise.
#define MIN_CURRENT_A        6.0f
// Current used at power-up and whenever the LoRa link has been silent for
// LINK_TIMEOUT_MS. Set to 0 to pause charging when the link is lost.
#define FALLBACK_CURRENT_A   6.0f
// After the available current dropped below 6 A, wait this long with enough
// current before offering it again (avoids contactor chatter).
#define RESUME_DELAY_MS      30000UL
// When charging must pause, give the car this long to stop drawing current
// before the contactor is opened under it.
#define PAUSE_GRACE_MS       3000UL
// After a fault the Control Pilot is held at -12 V this long, then re-checked.
#define FAULT_HOLD_MS        5000UL

// Control Pilot sense divider (values in ohm) - must match the schematic.
//   CP --R6--+--R7-- GND
//            +--R8-- 3V3
//            +------ PIN_CP_IN
#define R6_OHM   330000.0f
#define R7_OHM    47000.0f
#define R8_OHM    75000.0f
#define VDD_VOLT      3.3f
// Fine-tuning: measure the real CP voltage with a multimeter in state A
// (no car, should be +12 V) and compare with the value printed on the serial
// port. CP_GAIN = measured / printed.
#define CP_GAIN       1.0f

// Control Pilot thresholds, in volts at the CP line (nominal 12 / 9 / 6 V).
#define CP_STATE_A_MIN  10.5f
#define CP_STATE_B_MIN   7.5f
#define CP_STATE_C_MIN   4.5f

#define CP_PWM_FREQ_HZ  1000
#define CP_PWM_BITS     12
#define CP_PWM_FULL     ((1 << CP_PWM_BITS) - 1)  // constant +12 V
#define CP_SAMPLE_US    5000   // look for the CP peak over 5 PWM periods
#define STATE_DEBOUNCE  3      // identical readings needed before changing state

// ------------------------------------------------------------ Globals ------
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

volatile bool radioIrq = false;  // set by DIO1: packet received or transmission finished
bool radioOk = false;
bool radioSending = false;
uint32_t radioSendStartMs = 0;
uint32_t radioRetryMs = 0;

bool everHeard = false;          // a valid setpoint has been received since power-up
uint32_t lastHeardMs = 0;
float requestedAmps = 0;         // last setpoint received from the remote
float lastRssi = 0;

char state = 'a';                // 'a' no car, 'b' connected, 'c' charging, 'f' fault
char candidate = 'a';
uint8_t candidateCount = 0;
float cpVolts = 12;
float offeredAmps = 0;           // current advertised to the car right now (0 = paused)

bool everBelowMin = false;
uint32_t lastBelowMinMs = 0;
uint32_t pauseStartMs = 0;
bool pauseGrace = false;          // contactor still closed while the car ramps down
bool wasOffering = false;
bool relayOn = false;
uint32_t faultStartMs = 0;
uint32_t lastPrintMs = 0;

// ------------------------------------------------------------ CP output ----
// duty = 0 -> constant -12 V, CP_PWM_FULL -> constant +12 V
void cpWrite(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_CP_OUT, duty);
#else
  ledcWrite(0, duty);
#endif
}

void cpBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_CP_OUT, CP_PWM_FREQ_HZ, CP_PWM_BITS);
#else
  ledcSetup(0, CP_PWM_FREQ_HZ, CP_PWM_BITS);
  ledcAttachPin(PIN_CP_OUT, 0);
#endif
  cpWrite(CP_PWM_FULL);
}

// IEC 61851-1: between 6 A and 51 A the duty cycle in % is current / 0.6
// (10 A -> 16.7 %, 16 A -> 26.7 %).
uint32_t dutyForAmps(float amps) {
  return (uint32_t)(amps / 0.6f / 100.0f * (1 << CP_PWM_BITS) + 0.5f);
}

// ------------------------------------------------------------ CP input -----
// The CP line is a 1 kHz square wave; the car's state is in its positive peak.
float readCpPeakVolts() {
  uint32_t peakMv = 0;
  uint32_t start = micros();
  while (micros() - start < CP_SAMPLE_US) {
    uint32_t mv = analogReadMilliVolts(PIN_CP_IN);
    if (mv > peakMv) peakMv = mv;
  }
  // Undo the divider: Vpin * (1/R6 + 1/R7 + 1/R8) = Vcp/R6 + VDD/R8
  float vPin = peakMv / 1000.0f;
  float g = 1.0f / R6_OHM + 1.0f / R7_OHM + 1.0f / R8_OHM;
  return CP_GAIN * R6_OHM * (vPin * g - VDD_VOLT / R8_OHM);
}

char classifyCp(float volts) {
  if (volts > CP_STATE_A_MIN) return 'a';
  if (volts > CP_STATE_B_MIN) return 'b';
  if (volts > CP_STATE_C_MIN) return 'c';
  return 'f';
}

// ------------------------------------------------------------ Radio --------
void IRAM_ATTR onRadioIrq() {
  radioIrq = true;
}

void radioBegin() {
  int16_t err = xiaoRadioBegin(radio, onRadioIrq);
  radioOk = (err == RADIOLIB_ERR_NONE);
  radioSending = false;
  radioIrq = false;
  if (!radioOk) {
    Serial.print("LoRa radio not responding, error ");
    Serial.println(err);
  }
}

void radioSendStatus(uint8_t seq) {
  LinkStatus status;
  status.magic = LINK_MAGIC;
  status.type = LINK_TYPE_STATUS;
  status.stationId = LINK_STATION_ID;
  status.seq = seq;
  status.state = state;
  status.offeredDeciAmps = (uint16_t)(offeredAmps * 10.0f + 0.5f);
  // Non-blocking: the Control Pilot must keep being watched while the packet is on air.
  if (radio.startTransmit((uint8_t *)&status, sizeof(status)) == RADIOLIB_ERR_NONE) {
    radioSending = true;
    radioSendStartMs = millis();
  } else {
    radio.startReceive();
  }
}

void radioService() {
  uint32_t now = millis();
  if (!radioOk) {
    if (now - radioRetryMs > 10000UL) {
      radioRetryMs = now;
      radioBegin();
    }
    return;
  }
  if (radioSending && now - radioSendStartMs > 2000UL) {
    radioIrq = true;  // transmission never finished: force back to receive
  }
  if (!radioIrq) return;
  radioIrq = false;

  if (radioSending) {
    radioSending = false;
    radio.finishTransmit();
    radio.startReceive();
    return;
  }

  LinkSetpoint msg;
  size_t len = radio.getPacketLength();
  int16_t err = radio.readData((uint8_t *)&msg, sizeof(msg));
  bool valid = err == RADIOLIB_ERR_NONE && len == sizeof(msg) &&
               msg.magic == LINK_MAGIC && msg.type == LINK_TYPE_SETPOINT &&
               msg.stationId == LINK_STATION_ID;
  if (!valid) {
    radio.startReceive();
    return;
  }
  requestedAmps = msg.deciAmps / 10.0f;
  everHeard = true;
  lastHeardMs = now;
  lastRssi = radio.getRSSI();
  radioSendStatus(msg.seq);
}

bool linkAlive() {
  return everHeard && millis() - lastHeardMs < LINK_TIMEOUT_MS;
}

// ------------------------------------------------------------ Setup --------
void setup() {
  // Relay first, so the contactor is guaranteed open while everything else starts.
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  pinMode(PIN_LED_CAR, OUTPUT);
  pinMode(PIN_LED_CHARGE, OUTPUT);
  pinMode(PIN_LED_LINK, OUTPUT);
  pinMode(PIN_WIO_LED, OUTPUT);
  digitalWrite(PIN_LED_CAR, LOW);
  digitalWrite(PIN_LED_CHARGE, LOW);
  digitalWrite(PIN_LED_LINK, LOW);
  digitalWrite(PIN_WIO_LED, LOW);

  Serial.begin(115200);

  cpBegin();  // Control Pilot at constant +12 V
  analogSetPinAttenuation(PIN_CP_IN, ADC_11db);

  radioBegin();
}

// ------------------------------------------------------------ Loop ---------
void loop() {
  radioService();
  uint32_t now = millis();

  // ---- How much current may be offered right now?
  float allowed = linkAlive() ? requestedAmps : FALLBACK_CURRENT_A;
  if (allowed > MAX_CURRENT_A) allowed = MAX_CURRENT_A;
  if (allowed < MIN_CURRENT_A) {
    everBelowMin = true;
    lastBelowMinMs = now;
  }
  bool offering = !everBelowMin || now - lastBelowMinMs >= RESUME_DELAY_MS;
  if (wasOffering && !offering && relayOn) {
    pauseGrace = true;  // was charging: let the car ramp down before opening
    pauseStartMs = now;
  }
  wasOffering = offering;

  // ---- What is the car doing?
  cpVolts = readCpPeakVolts();
  char reading = classifyCp(cpVolts);
  if (reading == candidate) {
    if (candidateCount < 255) candidateCount++;
  } else {
    candidate = reading;
    candidateCount = 1;
  }
  if (candidate != state && candidateCount >= STATE_DEBOUNCE) {
    if (state == 'f') {
      // Stay in fault (CP at -12 V) for FAULT_HOLD_MS; the re-check below decides.
    } else {
      state = candidate;
      if (state == 'f') faultStartMs = now;
    }
  }
  if (state == 'f' && now - faultStartMs >= FAULT_HOLD_MS) {
    // Retry: back to +12 V with the relay open, and let the readings decide again.
    state = 'a';
    candidate = 'a';
    candidateCount = 0;
  }

  // ---- Drive Control Pilot, relay and LEDs
  relayOn = false;
  offeredAmps = 0;
  switch (state) {
    case 'a':  // no car: constant +12 V
      cpWrite(CP_PWM_FULL);
      break;
    case 'b':  // car connected, not asking for power
    case 'c':  // car asking for power
      if (offering) {
        offeredAmps = allowed;
        cpWrite(dutyForAmps(allowed));
        relayOn = (state == 'c');
      } else {
        // Not enough current: constant +12 V tells the car to stop charging.
        cpWrite(CP_PWM_FULL);
        if (state != 'c' || now - pauseStartMs >= PAUSE_GRACE_MS) pauseGrace = false;
        relayOn = pauseGrace;
      }
      break;
    default:   // fault: constant -12 V
      cpWrite(0);
      break;
  }
  digitalWrite(PIN_RELAY, relayOn ? HIGH : LOW);

  bool blinkSlow = (now / 500) % 2;
  bool blinkFast = (now / 125) % 2;
  if (state == 'f') {
    digitalWrite(PIN_LED_CAR, blinkFast);
    digitalWrite(PIN_LED_CHARGE, !blinkFast);
  } else {
    digitalWrite(PIN_LED_CAR, state != 'a');
    // Blinking: charging. Steady: car wants power but charging is paused.
    digitalWrite(PIN_LED_CHARGE, state == 'c' && (relayOn ? blinkSlow : true));
  }
  digitalWrite(PIN_LED_LINK, linkAlive());
  digitalWrite(PIN_WIO_LED, linkAlive());  // same, on the LoRa board itself

  // ---- Debug output
  if (now - lastPrintMs >= 1000) {
    lastPrintMs = now;
    Serial.printf("state=%c cp=%.1fV offered=%.1fA relay=%d link=%s requested=%.1fA rssi=%.0fdBm\n",
                  state, cpVolts, offeredAmps, relayOn, linkAlive() ? "ok" : "lost",
                  requestedAmps, lastRssi);
  }
}
