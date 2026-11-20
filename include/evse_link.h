// LoRa link shared by the charging station and the remote.
// Both firmwares include this file, so radio settings and packet layout
// can only be changed in one place.
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------- Radio ----
// Both ends must use identical settings.
// 868.1 MHz sits in the EU868 g1 sub-band: max 25 mW ERP (14 dBm), 1 % duty cycle.
// The Wio-SX1262 covers 862-930 MHz: for US/AU use e.g. 915.0 and a matching antenna.
#define LINK_FREQUENCY_MHZ   868.1
#define LINK_BANDWIDTH_KHZ   125.0
#define LINK_SPREADING       7      // SF7: ~40 ms per packet. Raise (max 12) for more range.
#define LINK_CODING_RATE     5      // 4/5
#define LINK_SYNC_WORD       0x12   // standard "private network" value (0x34 = LoRaWAN)
#define LINK_TX_POWER_DBM    14
#define LINK_PREAMBLE        8

// ------------------------------------------------------------- Protocol ----
// Station ID lets several stations/remotes share the same channel.
#define LINK_STATION_ID      1

#define LINK_MAGIC           0xEC
#define LINK_TYPE_SETPOINT   0x01   // remote  -> station
#define LINK_TYPE_STATUS     0x02   // station -> remote (acknowledgement)

// How often the remote repeats the setpoint, and how long the station keeps
// trusting the last one. With SF7 one packet every 10 s is ~0.4 % duty cycle.
// If you raise the spreading factor, raise the interval too (airtime doubles
// per SF step) to stay below 1 %.
#define LINK_SEND_INTERVAL_MS   10000UL
#define LINK_MIN_INTERVAL_MS     5000UL   // never send more often than this
#define LINK_TIMEOUT_MS         60000UL   // station falls back after this silence

// Multi-byte fields are little-endian (native on the ESP32-S3).
struct __attribute__((packed)) LinkSetpoint {
  uint8_t  magic;      // LINK_MAGIC
  uint8_t  type;       // LINK_TYPE_SETPOINT
  uint8_t  stationId;  // LINK_STATION_ID
  uint8_t  seq;        // incremented by the remote on every packet
  uint16_t deciAmps;   // available current in 0.1 A (160 = 16.0 A, 0 = pause)
};

struct __attribute__((packed)) LinkStatus {
  uint8_t  magic;           // LINK_MAGIC
  uint8_t  type;            // LINK_TYPE_STATUS
  uint8_t  stationId;       // LINK_STATION_ID
  uint8_t  seq;             // seq of the setpoint being acknowledged
  char     state;           // 'a' no car, 'b' connected, 'c' charging, 'f' fault
  uint16_t offeredDeciAmps; // current actually advertised to the car (0 = paused)
};
