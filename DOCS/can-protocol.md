# CAN Bus Protocol

## Bus Configuration

| Parameter | Value |
|-----------|-------|
| Bit Rate | 500 kbps |
| ID Format | Standard (11-bit) |
| Mode | TWAI Normal |
| Filter | Accept all |

## Message Summary

### Transmitted by Reservoir

| CAN ID | Name | DLC | Cycle | Description |
|--------|------|-----|-------|-------------|
| 0x04 | FirmwareVersionReport | 6 | Once at boot | Device MAC + firmware version |
| 0x3E | WaterTankLevels | 3 | 1000ms | Fill level of all 3 tanks |

### Received by Reservoir

| CAN ID | Name | DLC | Source | Description |
|--------|------|-----|--------|-------------|
| 0x00 | OtaUpdateNotification | 3 | Headwaters | MAC-targeted OTA trigger |
| 0x01 | WiFiConfig | 1-8 | Headwaters | Chunked WiFi credential provisioning |
| 0x02 | DiscoveryTrigger | 0 | Headwaters | Broadcast mDNS discovery trigger |

## Message 0x3E: WaterTankLevels

**Direction:** Reservoir -> Bus (broadcast)
**DLC:** 3 bytes
**Cycle time:** 1000ms (active), 2000ms (probing)
**DBC definition:** `BO_ 62 WaterTankLevels: 3 Reservoir`

### Byte Layout

| Byte | Signal Name | Type | Range | Unit | Description |
|------|-------------|------|-------|------|-------------|
| 0 | FreshWaterLevel | uint8 | 0-100 | % | Fresh water tank fill level |
| 1 | GreyWaterLevel | uint8 | 0-100 | % | Grey water tank fill level |
| 2 | BlackWaterLevel | uint8 | 0-100 | % | Black water tank fill level |

### DBC Signal Definitions

```dbc
SG_ FreshWaterLevel : 7|8@0+ (1,0) [0|100] "%" Headwaters
SG_ GreyWaterLevel : 15|8@0+ (1,0) [0|100] "%" Headwaters
SG_ BlackWaterLevel : 23|8@0+ (1,0) [0|100] "%" Headwaters
```

Signal encoding: big-endian (Motorola), unsigned, factor=1, offset=0, no scaling.

### Encoding Examples

**All tanks empty:**
```
CAN ID: 0x3E  DLC: 3  Data: [0x00, 0x00, 0x00]
Fresh: 0%  Grey: 0%  Black: 0%
```

**Fresh half full, grey quarter, black empty:**
```
CAN ID: 0x3E  DLC: 3  Data: [0x32, 0x19, 0x00]
Fresh: 50%  Grey: 25%  Black: 0%
```

**All tanks full:**
```
CAN ID: 0x3E  DLC: 3  Data: [0x64, 0x64, 0x64]
Fresh: 100%  Grey: 100%  Black: 100%
```

**Fresh 75%, grey full, black 25%:**
```
CAN ID: 0x3E  DLC: 3  Data: [0x4B, 0x64, 0x19]
Fresh: 75%  Grey: 100%  Black: 25%
```

### Current Sensor Quantization

With the current contactless binary sensors, values are quantized to 5 discrete levels:

| Hex Value | Decimal | Level |
|-----------|---------|-------|
| 0x00 | 0 | Empty |
| 0x19 | 25 | Quarter |
| 0x32 | 50 | Half |
| 0x4B | 75 | Three-quarter |
| 0x64 | 100 | Full |

### Future Analog Sensor Values

When analog sensors are installed, values can be any integer from 0 to 100. The CAN message format remains identical. Receivers do not need to distinguish between sensor types; they simply display the percentage.

Example with analog sensors:
```
CAN ID: 0x3E  DLC: 3  Data: [0x43, 0x1D, 0x08]
Fresh: 67%  Grey: 29%  Black: 8%
```

## Message 0x04: FirmwareVersionReport

**Direction:** Reservoir -> Bus (broadcast)
**DLC:** 6 bytes
**Cycle time:** Once at startup
**Shared with:** All TrailCurrent modules

| Byte | Signal Name | Description |
|------|-------------|-------------|
| 0 | MacAddressByte4 | WiFi MAC address byte 4 |
| 1 | MacAddressByte5 | WiFi MAC address byte 5 |
| 2 | MacAddressByte6 | WiFi MAC address byte 6 |
| 3 | VersionMajor | Firmware major version |
| 4 | VersionMinor | Firmware minor version |
| 5 | VersionPatch | Firmware patch version |

The MAC bytes combined with the firmware version allow Headwaters to track which firmware each device is running.

## Message 0x00: OtaUpdateNotification

**Direction:** Headwaters -> Bus (broadcast, MAC-targeted)
**DLC:** 3 bytes

| Byte | Description |
|------|-------------|
| 0 | Target MAC byte 4 |
| 1 | Target MAC byte 5 |
| 2 | Target MAC byte 6 |

Only the device whose MAC matches enters OTA mode. All other devices ignore the message.

## Message 0x01: WiFiConfig

**Direction:** Headwaters -> Bus
**DLC:** 1-8 bytes (varies by sub-message)

Multi-message chunked protocol for WiFi credential provisioning. See the TrailCurrent CAN Bus Reference for the full specification.

| Sub-type (byte 0) | Description |
|--------------------|-------------|
| 0x01 | Start: declares SSID/password lengths and chunk counts |
| 0x02 | SSID chunk: 6 bytes of SSID data per message |
| 0x03 | Password chunk: 6 bytes of password data per message |
| 0x04 | End: XOR checksum for validation |

## Message 0x02: DiscoveryTrigger

**Direction:** Headwaters -> Bus (broadcast)
**DLC:** 0 bytes (no payload)

All modules with WiFi credentials respond by connecting to WiFi and advertising via mDNS. Reservoir advertises with:

| TXT Record | Value |
|------------|-------|
| type | reservoir |
| canid | 0x3E |
| fw | (firmware version string) |

## Headwaters Integration

Headwaters (the CAN-to-MQTT bridge) receives message 0x3E and publishes it as JSON over MQTT:

```json
{
  "fresh": 50,
  "grey": 25,
  "black": 0
}
```

The mobile app (Kotlin `WaterState` model and React Native `TankBar` component) expects integer percentage values in this format. The existing backend API endpoint (`vehicleRepository.getWater()`) and WebSocket event (`WebSocketEvent.Water`) are already designed for percentage-based reporting.
