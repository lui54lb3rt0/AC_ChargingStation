⚠️ WARNING — READ BEFORE USE

This Arduino Uno code and Shield are provided as a starter project for educational purposes only. They are intended to help you learn about EV charging concepts and are sufficient to demonstrate basic charging station functionality. This project is not compliant with safety standards or certifications required for production use.

ELECTRICAL HAZARD: Working with AC mains power is dangerous and can result in serious injury, death, electric shock, fire, or property damage. If you are not experienced with high-voltage electrical systems, consult a licensed electrician before attempting to build or test this project.

By using this code and hardware, you accept full responsibility for your safety and any consequences that may arise.

This is a repository for a learning and development project (not a finished product).
This repository contains:
- Code for Arduino Uno to work as an EV AC Charging Station.
- KiCAD Schematic and PCB of a Shield for Arduino Uno to make the hardware for an EV AC Charging Station.
- Gerber files to fabricate the Shield.
- BOM to assemble the Shield.

There are two versions:
- V2.2: Arduino Uno shield with a fixed charging current (`EVSE_V2_2.*`, firmware in `src/uno`). This is the version the course below is based on.
- V3.0: Seeed XIAO ESP32S3 + Wio-SX1262 LoRa kit, where a second, identical kit sets the available current remotely (`EVSE_V3_0.*`, firmware in `src/station` and `src/remote`). The schematic is done, the PCB layout is not. See [docs/ESP32_LoRa_Upgrade.md](docs/ESP32_LoRa_Upgrade.md).

Instructions to execute this learning project: https://www.udemy.com/course/electric-vehicle-charging-from-theory-to-practice/?referralCode=AC67637479A66E6618A2
