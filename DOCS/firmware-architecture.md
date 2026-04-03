# Firmware Architecture

## Overview

The Reservoir firmware is an ESP-IDF application that polls contactless water level sensors and broadcasts tank fill percentages over a CAN bus. It follows the same architectural patterns as other TrailCurrent modules (Ampline, Solstice, Picket) with dedicated FreeRTOS tasks for CAN and application logic.

## Task Structure

| Task | Core | Priority | Stack | Function |
|------|------|----------|-------|----------|
| Main (app_main) | 0 | 1 (default) | default | Sensor polling loop |
| TWAI | 1 | 5 | 4096 B | CAN bus TX/RX and error handling |
| OTA | any | 3 | 8192 B | On-demand: WiFi + HTTP OTA server |
| Discovery | any | 3 | 8192 B | On-demand: mDNS registration |

The OTA and Discovery tasks are mutually exclusive and only run when triggered by a CAN message. They are created dynamically and delete themselves on completion.

## Data Flow

```
Sensors (GPIO)          Shared Memory           CAN Bus
 ┌──────────┐          ┌──────────────┐        ┌──────────┐
 │ 12 GPIOs │──poll──> │ tank_levels  │──read─>│ TWAI TX  │──> CAN ID 0x3E
 │ (500ms)  │          │ [3 x uint8]  │        │ (1000ms) │
 └──────────┘          └──────────────┘        └──────────┘
   Main Task            volatile array          TWAI Task
```

1. **Main task** reads all 12 GPIO pins every 500ms
2. For each tank, it determines the highest triggered sensor and writes the percentage to `tank_levels[]`
3. **TWAI task** reads `tank_levels[]` every 1000ms and packs it into a 3-byte CAN message
4. Single-writer (main task) / single-reader (TWAI task) pattern with `volatile` — no mutex needed

## Sensor-to-Percentage Conversion

Each tank has 4 sensors mounted at physical water levels corresponding to 25%, 50%, 75%, and 100%. The sensors are indexed from lowest (index 0 = 25%) to highest (index 3 = 100%).

```c
static uint8_t read_tank_level(const tank_config_t *tank)
{
    uint8_t level = 0;
    for (int s = 0; s < NUM_SENSORS; s++) {
        if (gpio_get_level(tank->pins[s])) {
            level = (s + 1) * 25;
        }
    }
    return level;
}
```

The loop iterates from the lowest sensor upward. Each HIGH sensor overwrites the level, so the final value is the highest triggered sensor. This handles the normal case where water triggers all sensors below the surface.

**Edge cases:**

| Scenario | Sensor States (25/50/75/100) | Result |
|----------|------------------------------|--------|
| Empty tank | LOW/LOW/LOW/LOW | 0% |
| Quarter full | HIGH/LOW/LOW/LOW | 25% |
| Half full | HIGH/HIGH/LOW/LOW | 50% |
| Three-quarter full | HIGH/HIGH/HIGH/LOW | 75% |
| Full | HIGH/HIGH/HIGH/HIGH | 100% |
| No tank connected | LOW/LOW/LOW/LOW | 0% |
| Sensor skip (50% stuck LOW) | HIGH/LOW/HIGH/LOW | 75% |

The last case (sensor skip) reports the highest triggered sensor regardless of gaps. This is intentional — a stuck-low sensor should not suppress the reading from higher sensors.

## GPIO Configuration

All 12 sensor GPIOs are configured in a single `gpio_config()` call at startup:

- **Mode:** Input
- **Pull-up:** Disabled
- **Pull-down:** Enabled (internal)
- **Interrupt:** Disabled (polling-based)

The internal pull-down ensures a clean LOW reading when no sensor is connected or when the sensor is not triggered. The contactless sensors drive the GPIO HIGH when water is detected.

## CAN Bus State Machine

The TWAI task implements the same bus state machine used across all TrailCurrent modules:

```
                 ┌─────────────┐
     boot ──────>│  TX_ACTIVE  │<──── TX_SUCCESS or RX_DATA
                 │  (1000ms)   │
                 └──────┬──────┘
                        │ 3x TX_FAILED
                        v
                 ┌─────────────┐
                 │ TX_PROBING  │───── TX_SUCCESS ────> TX_ACTIVE
                 │  (2000ms)   │
                 └──────┬──────┘
                        │ BUS_OFF
                        v
                 ┌─────────────┐
                 │  RECOVERY   │───── BUS_RECOVERED ──> TX_PROBING
                 │  (waiting)  │
                 └─────────────┘
```

- **TX_ACTIVE:** Normal operation, transmits every 1000ms
- **TX_PROBING:** No peers on bus, slows to 2000ms to reduce error load
- **RECOVERY:** Bus-off detected, driver recovery in progress, no transmissions

## Startup Sequence

1. Initialize NVS and load WiFi credentials (if previously provisioned)
2. Initialize Discovery and OTA subsystems
3. Configure all 12 sensor GPIOs with pull-downs
4. Log hostname and pin assignments
5. Start TWAI task on core 1
6. Broadcast firmware version on CAN ID 0x04
7. Enter main loop: poll sensors every 500ms

## Source Files

| File | Lines | Purpose |
|------|-------|---------|
| `main.c` | ~250 | Sensor GPIO config, polling, TWAI task, CAN message packing |
| `wifi_config.c` | ~280 | NVS credential storage, CAN-based WiFi provisioning state machine |
| `wifi_config.h` | ~15 | WiFi config public API |
| `ota.c` | ~220 | HTTP OTA server, firmware write, CAN trigger handling |
| `ota.h` | ~28 | OTA public API |
| `discovery.c` | ~175 | mDNS advertisement, Headwaters confirmation endpoint |
| `discovery.h` | ~28 | Discovery public API |

## Timing Summary

| Event | Interval | Notes |
|-------|----------|-------|
| Sensor poll | 500ms | Main task loop delay |
| CAN TX (active) | 1000ms | Normal operation with peers |
| CAN TX (probing) | 2000ms | No peers detected |
| WiFi config timeout | 5s | Resets chunked credential state machine |
| OTA timeout | 3 min | Disconnects WiFi if no upload received |
| Discovery timeout | 3 min | Disconnects WiFi if no confirmation |

## Memory Usage

The firmware has minimal RAM requirements:

- **Shared state:** 3 bytes (`tank_levels[3]`, volatile uint8)
- **No heap allocations** in the sensor polling or CAN transmission paths
- **Stack sizes:** Main task uses default (~3.5 KB), TWAI task uses 4 KB
- **OTA/Discovery:** 8 KB stack each, only allocated when triggered
