# Sensor Wiring Guide

## Sensor Overview

The Reservoir module uses contactless water level sensors that detect the presence of water through the tank wall without any penetration. Each sensor outputs a digital HIGH signal when water is present at its mounting position.

### Sensor Requirements

- **Type:** Contactless capacitive water level sensor (non-contact, external mount)
- **Output:** Digital HIGH when water detected, LOW when dry
- **Voltage:** 3.3V compatible (ESP32-S3 GPIO voltage)
- **Quantity:** 4 per tank, up to 12 total for 3 tanks

### Supported Sensors

Any contactless water level sensor with a digital HIGH/LOW output will work. Common options include:

- XKC-Y25-T12V (non-contact liquid level sensor)
- XKC-Y25-NPN or XKC-Y25-PNP variants (ensure 3.3V compatibility)

Verify the sensor's output voltage matches 3.3V logic levels. Some sensors are 5V or 12V output and require a voltage divider or level shifter.

## Wiring Diagram

### Per-Sensor Connection

Each sensor has 3 wires:

```
Sensor              ESP32-S3
┌──────────┐       ┌──────────┐
│ VCC (red)│───────│ 3.3V     │
│ GND (blk)│───────│ GND      │
│ OUT (yel)│───────│ GPIO xx  │
└──────────┘       └──────────┘
```

The ESP32-S3 GPIO is configured with an internal pull-down resistor. When the sensor detects water, it drives the GPIO HIGH. When dry, the pull-down holds the GPIO LOW.

### GPIO Pin Assignments

#### Fresh Water Tank

| Level | GPIO | Wire Color Suggestion |
|-------|------|-----------------------|
| 25% | 4 | Blue |
| 50% | 5 | Green |
| 75% | 6 | Yellow |
| 100% | 7 | Red |

#### Grey Water Tank

| Level | GPIO | Wire Color Suggestion |
|-------|------|-----------------------|
| 25% | 8 | Blue |
| 50% | 9 | Green |
| 75% | 10 | Yellow |
| 100% | 11 | Red |

#### Black Water Tank

| Level | GPIO | Wire Color Suggestion |
|-------|------|-----------------------|
| 25% | 12 | Blue |
| 50% | 13 | Green |
| 75% | 14 | Yellow |
| 100% | 43 | Red |

### Power Wiring

All sensors share 3.3V and GND from the Waveshare board:

```
3.3V ──┬── Sensor 1 VCC
       ├── Sensor 2 VCC
       ├── ...
       └── Sensor 12 VCC

GND  ──┬── Sensor 1 GND
       ├── Sensor 2 GND
       ├── ...
       └── Sensor 12 GND
```

### CAN Bus Wiring

The CAN bus uses the onboard TJA1051 transceiver on the Waveshare board:

| Terminal | Connection |
|----------|------------|
| CANH | CAN bus high wire |
| CANL | CAN bus low wire |
| GND | CAN bus ground (shared) |

A 120-ohm termination resistor is required at each end of the CAN bus.

## Sensor Mounting

### Placement

Mount sensors on the **outside** of the tank wall at the desired measurement heights:

```
Tank (side view)
┌─────────────────┐
│                  │ ← 100% sensor (near top, leave room for expansion)
│                  │
│                  │ ← 75% sensor
│                  │
│                  │ ← 50% sensor (midpoint)
│                  │
│                  │ ← 25% sensor
│                  │
└──────────────────┘
```

### Mounting Guidelines

1. **Clean the tank surface** where each sensor will be mounted. Remove any dirt, grease, or residue.
2. **Adhere sensors firmly** to the tank wall. Most contactless sensors include adhesive backing or mounting brackets.
3. **Ensure flat contact** between the sensor face and the tank wall. Air gaps reduce sensitivity.
4. **Avoid mounting near:**
   - Tank seams or welds (uneven surface)
   - Heating elements or hot water lines
   - Metal reinforcements or brackets (may interfere with capacitive sensing)
5. **Vertical spacing** should divide the usable tank height into quarters. The 25% sensor goes at 1/4 height from the bottom, 100% near the top but below the overflow level.

### Tank Wall Compatibility

Contactless capacitive sensors work through:
- **Plastic tanks** (polyethylene, polypropylene) - best performance
- **Fiberglass tanks** - good performance
- **Thin metal tanks** - reduced sensitivity, may not work reliably

They do **not** work through thick metal walls. If your tank is metal, consider in-tank sensors with a sealed feedthrough.

## Partial Tank Configurations

Not all three tanks need to be connected. The firmware reports 0% for any tank with no sensors wired:

| Configuration | CAN Output |
|---------------|------------|
| Fresh only | `[level, 0, 0]` |
| Fresh + Grey | `[level, level, 0]` |
| All three | `[level, level, level]` |
| Grey + Black only | `[0, level, level]` |

No firmware changes are needed. Unwired GPIOs read LOW (0%) due to the internal pull-down resistors.

## Troubleshooting

### Sensor Always Reads LOW (0%)

1. **Check wiring:** Verify VCC, GND, and signal connections
2. **Check voltage:** Measure sensor output with a multimeter while submerged — should be ~3.3V
3. **Check GPIO:** Verify the correct GPIO pin is connected (see pin assignment table)
4. **Check mounting:** Ensure sensor is firmly against the tank wall with no air gap
5. **Check tank material:** Metal tanks may block the capacitive signal

### Sensor Always Reads HIGH (stuck at that level)

1. **Check for moisture** on the outside of the tank near the sensor
2. **Check for condensation** between the sensor and tank wall
3. **Remove and re-mount** the sensor on a dry surface
4. **Check wire routing:** Signal wires running parallel to power wires can pick up noise

### Erratic Readings (level jumps between values)

1. **Add wire shielding** if signal cables run longer than 1 meter
2. **Keep sensor wires away** from high-current or high-frequency sources (inverters, motors)
3. **Check power supply:** Ensure 3.3V rail is clean and not sagging under load

### Only Some Tanks Report Data

1. **Check which GPIOs** are connected — each tank uses a specific set of 4 GPIOs
2. **Monitor serial output:** The firmware logs all pin assignments at startup
3. **Verify the GPIO numbers** match the physical wiring (not the board silkscreen pin numbers)

### CAN Bus Not Transmitting

1. **Check CAN wiring:** CANH to CANH, CANL to CANL, with 120-ohm termination
2. **Monitor serial output:** Look for "TWAI bus-off" or "error passive" messages
3. **Verify at least one other CAN node** is on the bus (TWAI normal mode requires ACK from a peer)
4. **Check the bus is not in probing mode** (serial output will say "entering slow probe") — this is normal when no peers are connected

## Modifying Pin Assignments

If you need to use different GPIOs (due to wiring constraints or a different board), edit the `tanks` array in `main/main.c`:

```c
static const tank_config_t tanks[NUM_TANKS] = {
    { "fresh", { GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7  } },
    { "grey",  { GPIO_NUM_8,  GPIO_NUM_9,  GPIO_NUM_10, GPIO_NUM_11 } },
    { "black", { GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_43 } },
};
```

**Avoid these GPIOs:**
- GPIO 0: Boot strapping pin
- GPIO 3: Boot strapping pin
- GPIO 15: CAN TX (used by onboard transceiver)
- GPIO 16: CAN RX (used by onboard transceiver)
- GPIO 19-20: USB D-/D+ (used for USB CDC console)
- GPIO 26-32: Flash SPI (reserved on most ESP32-S3 modules)
- GPIO 33-37: PSRAM (reserved on WROVER variants)
- GPIO 45-46: Boot strapping pins
