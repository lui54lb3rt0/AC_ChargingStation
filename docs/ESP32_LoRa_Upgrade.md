# V3.0 - XIAO ESP32S3 + Wio-SX1262 LoRa

The V3.0 board replaces the Arduino Uno with the Seeed **XIAO ESP32S3 & Wio-SX1262 kit**
(Seeed part 102010611). A second, identical kit is the remote: it sends the available
current over LoRa and the station adjusts the Control Pilot duty cycle to match.

The same safety warning as in the README applies: this is an educational project, not a
certified product.

| File | What it is |
|---|---|
| `EVSE_V3_0.kicad_sch` / `.kicad_pro` | New schematic (KiCad 8). **The PCB layout still has to be done**, see below. |
| `EVSE_V3_0_Schematic.pdf` | Printout of the new schematic |
| `EVSE_V3.kicad_sym`, `EVSE_V3.pretty/` | Symbol and footprint for the XIAO |
| `src/station/` | Station firmware (`pio run -e station -t upload`) |
| `src/remote/` | Remote firmware (`pio run -e remote -t upload`) |
| `include/evse_link.h` | Radio settings and packet format shared by both |
| `include/xiao_sx1262.h` | How the LoRa board is wired to the ESP32-S3, shared by both |
| `src/uno/`, `EVSE_V2_2.*` | The original Arduino Uno version, unchanged |

## Shopping list

- 2 x Seeed XIAO ESP32S3 & Wio-SX1262 kit (one for the station, one for the remote).
- For the station: an external 868 MHz antenna with a U.FL (IPEX) to SMA pigtail. The FPC
  antenna in the kit is meant for testing only, and an antenna inside a metal enclosure
  does not work.
- The parts listed under "What changed in the circuit".

## The kit

The LoRa board plugs onto the board-to-board connector on top of the XIAO. The XIAO's own
header pins go down into the PCB. The radio's control pins are internal to the stack; only
its SPI bus also appears on header pins D8, D9 and D10, which the PCB leaves unconnected.

| Radio signal | ESP32-S3 GPIO |
|---|---|
| SCK / MISO / MOSI | 7 / 8 / 9 (= D8 / D9 / D10) |
| NSS | 41 |
| RESET | 42 |
| DIO1 | 39 |
| BUSY | 40 |
| Antenna switch | 38 |

These numbers come from the pin definitions the Meshtastic firmware uses for this kit.
Seeed's schematic of the LoRa board names the signals but not the GPIO numbers.

## Station pin map

| XIAO pin | GPIO | Function |
|---|---|---|
| D0 | 1 | Control Pilot sense (ADC), through the R6/R7/R8 divider |
| D1 | 2 | Control Pilot PWM, 1 kHz, to comparator U1 |
| D2 | 3 | not connected (boot strapping pin of the ESP32-S3) |
| D3 | 4 | Relay. R17 keeps it off while the pin floats during reset |
| D4 | 5 | LED D1: car connected |
| D5 | 6 | LED D2: charging |
| D6 | 43 | LED D4: LoRa link alive. Flickers at reset (boot log pin), harmless |
| D7 | 44 | free |
| D8-D10 | 7-9 | LoRa SPI bus: leave unconnected |

LED D3 is the power LED (wired to 3V3). The green LED on the LoRa board also shows the link
state. Serial debug goes over the USB-C connector, so the old serial header J4 is gone.

## What changed in the circuit, and why

| Part | V2.2 | V3.0 | Why |
|---|---|---|---|
| A1 | Arduino Uno | XIAO ESP32S3 + Wio-SX1262 on two 1x7 sockets | |
| U4 | - | Recom R-78E5.0-0.5 (12 V to 5 V) | the Uno's regulator used to make the 5 V |
| D6 | - | Schottky, 5 V rail to the XIAO 5V pin | USB and the 12 V supply can be connected together |
| R6 / R7 / R8 | 300k / 220k / 100k to 5 V | 330k / 47k / 75k to 3V3 | CP sense scaled for the 3.3 V ADC |
| R3 | to 5 V | to 3V3 | comparator threshold 1.65 V, the middle of the 3.3 V PWM |
| R2 | 1k | 470 | same base current for Q1 from a 3.3 V pin |
| R17 | - | 10k pull-down on the relay drive | relay stays off during reset |
| D5 | - | 1N4148W across the relay coil | flyback diode (V2.2 had none) |
| R12-R15 | 1k | 330 | LEDs now run from 3.3 V |
| J4 | serial header | removed | |

The 3.3 V rail comes from the regulator on the XIAO (its 3V3 pin); there is no separate
3.3 V regulator on the PCB.

With the new divider the ADC pin sees 2.14 V at CP = +12 V, 1.89 V at 9 V, 1.65 V at 6 V and
0.20 V at -12 V, so it can never exceed 3.3 V.

Use red, yellow or green LEDs. Blue and white ones need about 3 V and will barely light.

## Remote

The remote is just the second kit on a USB supply; nothing has to be wired.

By default the available current is typed on the serial monitor (115200 baud): `10` + Enter
for 10 A, `0` to pause charging. To take it from an energy meter, inverter or anything else,
replace `readAvailableCurrent()` in `src/remote/main.cpp`. A potentiometer on D0 is available
by uncommenting `SETPOINT_FROM_POT`.

Never power a LoRa board without its antenna attached: transmitting into nothing can damage it.

## How the station behaves

- The setpoint is clamped to `MAX_CURRENT_A` (16 A by default). **Set this to what your cable,
  contactor and breaker allow**; the remote can never exceed it.
- Below 6 A (the minimum the standard allows) charging is paused: the Control Pilot goes to a
  constant +12 V, and the contactor opens 3 s later. It resumes once at least 6 A has been
  available for 30 s (`RESUME_DELAY_MS`; 1 to 3 minutes is kinder to the car and the contactor).
- Until the first packet arrives, and whenever nothing has been heard for 60 s, the station
  uses `FALLBACK_CURRENT_A` (6 A by default; set it to 0 to pause instead).
- A fault (Control Pilot below about 4.5 V) opens the contactor and holds the Control Pilot at
  -12 V for 5 s, then the station checks again. The Uno version stayed in fault until reset.
- LEDs: D3 power, D1 car connected, D2 blinking = charging / steady = paused, D1 and D2
  flashing alternately = fault, D4 = LoRa link alive.

The radio link has no authentication: anyone nearby with a LoRa radio and this packet format
could change the setpoint, within 0 to `MAX_CURRENT_A`.

## Radio settings

868.1 MHz, 125 kHz, SF7, 14 dBm, one packet every 10 s plus an acknowledgement: about 0.4 %
airtime per unit, inside the 1 % EU868 limit. The module can transmit at up to 22 dBm, but
14 dBm is the legal limit on this frequency in Europe.

If the link is weak (several concrete floors, for example), raise `LINK_SPREADING` in
`include/evse_link.h`: each step adds roughly 2.5 to 3 dB of margin and doubles the airtime,
so raise `LINK_SEND_INTERVAL_MS` with it (about 20 s at SF9).

Range test before building anything: flash both kits, leave the station one where the
charger will be (a USB power bank is enough), type a current on the remote and watch the
replies. They print the RSSI. Reliable replies with RSSI better than about -110 dBm at SF7
mean comfortable margin.

## Finishing the PCB

The schematic passes KiCad's netlist export, and ERC shows only the kinds of messages the
V2.2 schematic already had. The layout is not done:

1. Copy `EVSE_V2_2.kicad_pcb` to `EVSE_V3_0.kicad_pcb` and open the `EVSE_V3_0` project.
2. *Tools > Update PCB from Schematic*, with "Delete footprints with no symbols" ticked. The
   Uno and J4 disappear, the new parts appear, and the parts that did not change keep their
   position.
3. Place and route. The board no longer has to be Uno-shaped; add mounting holes.
   - Keep the mains/contactor side (J1, K1) as far from everything else as it is now.
   - Leave the USB-C end of the XIAO reachable, and room above it for the LoRa board and
     its antenna cable.
4. Run DRC.

**Check before ordering boards:**

- The XIAO footprint (`EVSE_V3.pretty`) assumes rows 15.24 mm apart and this pin order, seen
  from the top with USB up: left row D0 to D6, right row 5V, GND, 3V3, D10, D9, D8, D7.
  Compare it with the board in your hand.
- The relay coil current against R2 = 470 ohm (about 5.5 mA of base current).

## First power-up

1. Flash both kits over USB, with the 12 V supply and mains disconnected. This replaces the
   Meshtastic firmware the kits ship with.
2. Power the station from 12 V with no car. The serial monitor should print `state=a` and
   `cp=` close to 12 V. Measure the real Control Pilot voltage at J3 with a multimeter and, if
   the printed value is off by more than a few tenths of a volt, set `CP_GAIN` = measured / printed.
3. Type a current on the remote. It should print the station's answer, and D4 on the station
   should light.
4. Test states B and C with an EV simulator/tester before connecting a car.
