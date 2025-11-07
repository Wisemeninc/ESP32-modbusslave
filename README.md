# ESP32-S3 Modbus RTU Slave with HW-519

This project implements a Modbus RTU slave on an ESP32-S3 using the HW-519 RS485 module. The HW-519 features automatic flow control, making the wiring simpler than traditional MAX485 modules.

## Features

- **Modbus RTU Slave** with configurable address (1-247)
- **Ten Holding Registers (0-9):**
  - Register 0: Sequential counter (increments on each access)
  - Register 1: Random number (updated every 5 seconds)
  - Register 2: Second counter (increments every second, auto-resets at 65535)
  - Registers 3-9: General purpose holding registers
- **HW-519 RS485 module** with automatic flow control
- **Half-duplex RS485** communication
- **WiFi Access Point** for configuration (active for 2 minutes after boot)
  - SSID: `ESP32-Modbus-Config`
  - Password: `modbus123`
  - Configure Modbus slave ID via web interface (no restart required)
  - View real-time statistics (requests, uptime, etc.)
  - View and monitor all holding registers in real-time
  - Persistent configuration stored in NVS (Non-Volatile Storage)

## Hardware Requirements

- ESP32-S3 Development Board (e.g., ESP32-S3-DevKitC-1)
- HW-519 RS485 module (automatic flow control)
- RS485 bus or USB-to-RS485 adapter for testing

## Pin Configuration

The HW-519 module has **automatic flow control** and only requires TX and RX connections:

| Function | GPIO Pin | HW-519 Pin | Notes                           |
|----------|----------|------------|----------------------------------|
| TX       | GPIO 18  | TXD        | UART1 transmit                  |
| RX       | GPIO 16  | RXD        | UART1 receive                   |
| GND      | GND      | GND        | Common ground                   |
| Power    | 5V       | VCC        | HW-519 needs 5V power           |

**Note:** Unlike MAX485 modules, the HW-519 does **not** require an RTS pin for flow control. The module automatically switches between transmit and receive modes.

## Wiring Diagram

```
ESP32-S3          HW-519          RS485 Bus
--------          ------          ---------
GPIO18 ---------> TXD
GPIO16 <--------- RXD
5V     ---------> VCC
GND    ---------- GND ----------- GND
                  A   ----------- A (Data+)
                  B   ----------- B (Data-)
```

**HW-519 Features:**
- Automatic transmit/receive switching (no flow control pin needed)
- Built-in 120Ω termination resistor (check jumper setting)
- Protection circuits included
- 3.3V logic compatible with 5V power supply

## Modbus Configuration

- **Slave Address:** Configurable via web interface (default: 1)
- **Baud Rate:** 9600
- **Data Bits:** 8
- **Parity:** None
- **Stop Bits:** 1
- **Mode:** RTU (RS485)

## WiFi Configuration Portal

On boot, the device creates a WiFi Access Point for 2 minutes:

- **SSID:** `ESP32-Modbus-Config`
- **Password:** `modbus123`
- **Web Interface:** http://192.168.4.1

### Web Interface Features

The web interface is organized into three tabs:

1. **Statistics Tab:**
   - Total Modbus requests
   - Read/Write request counts
   - Error count
   - System uptime
   - Current slave ID

2. **Registers Tab:**
   - Real-time view of all 10 holding registers
   - Values displayed in both decimal and hexadecimal
   - Auto-refreshes every 2 seconds
   - Register descriptions

3. **Configuration Tab:**
   - Change Modbus slave ID (1-247) without device restart
   - Changes are saved to non-volatile storage
   - Changes take effect immediately

**Note:** The WiFi AP automatically turns off 2 minutes after boot to save power. To access it again, restart the device.

## Modbus Register Map

| Register Address | Type           | Description                          | Access      |
|------------------|----------------|--------------------------------------|-------------|
| 0                | Holding (16bit)| Sequential counter                   | Read/Write  |
| 1                | Holding (16bit)| Random number (0-65535)             | Read/Write  |
| 2                | Holding (16bit)| Second counter (auto-reset)         | Read/Write  |
| 3-9              | Holding (16bit)| General purpose registers            | Read/Write  |

## Building and Flashing

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- ESP-IDF framework (automatically installed by PlatformIO)

### Build

```bash
platformio run
```

### Upload

```bash
platformio run --target upload
```

### Monitor

```bash
platformio run --target monitor
```

Or combine upload and monitor:

```bash
platformio run --target upload --target monitor
```

## Testing with Modbus Master

You can test this slave using various Modbus master tools:

### Using ModbusCLI (Command Line)

```bash
# Read holding registers
modbus read -s 1 -a 0 -n 2 /dev/ttyUSB0 9600

# Write to sequential counter
modbus write -s 1 -a 0 -v 100 /dev/ttyUSB0 9600
```

### Using QModMaster (GUI)

1. Configure connection:
   - Mode: RTU
   - Port: Your serial port
   - Baud: 9600
   - Data bits: 8
   - Parity: None
   - Stop bits: 1

2. Read registers:
   - Function: Read Holding Registers (0x03)
   - Slave ID: 1
   - Start Address: 0
   - Number of registers: 2

### Using Python pymodbus

```python
from pymodbus.client.sync import ModbusSerialClient

client = ModbusSerialClient(
    method='rtu',
    port='/dev/ttyUSB0',
    baudrate=9600,
    timeout=1,
    parity='N',
    stopbits=1,
    bytesize=8
)

client.connect()

# Read holding registers
result = client.read_holding_registers(address=0, count=2, unit=1)
print(f"Sequential Counter: {result.registers[0]}")
print(f"Random Number: {result.registers[1]}")

# Write to sequential counter
client.write_register(address=0, value=500, unit=1)

client.close()
```

## Expected Output

```
I (XXX) MB_SLAVE: ========================================
I (XXX) MB_SLAVE: ESP32-S3 Modbus RTU Slave with HW-519
I (XXX) MB_SLAVE: ========================================
I (XXX) MB_SLAVE: Slave Address: 1
I (XXX) MB_SLAVE: Baudrate: 9600
I (XXX) MB_SLAVE: UART Port: 1
I (XXX) MB_SLAVE: TX Pin: GPIO18 (HW-519 TXD)
I (XXX) MB_SLAVE: RX Pin: GPIO16 (HW-519 RXD)
I (XXX) MB_SLAVE: ========================================
I (XXX) MB_SLAVE: Starting WiFi AP for configuration...
I (XXX) MB_SLAVE: WiFi AP started. SSID:ESP32-Modbus-Config Password:modbus123 Channel:1
I (XXX) MB_SLAVE: Connect to http://192.168.4.1 to configure
I (XXX) MB_SLAVE: AP will automatically turn off in 2 minutes
I (XXX) MB_SLAVE: Starting web server on port: 80
I (XXX) MB_SLAVE: Holding registers initialized:
I (XXX) MB_SLAVE:   Register 0 (Sequential): 0
I (XXX) MB_SLAVE:   Register 1 (Random): 12345
I (XXX) MB_SLAVE: Modbus slave stack initialized successfully
I (XXX) MB_SLAVE: Modbus registers:
I (XXX) MB_SLAVE:   Address 0: Sequential Counter (Read/Write)
I (XXX) MB_SLAVE:   Address 1: Random Number (Read Only)
I (XXX) MB_SLAVE: Waiting for Modbus master requests...
I (XXX) MB_SLAVE: Random number updated: 54321
I (XXX) MB_SLAVE: HOLDING READ: Addr=0, Size=2, Value[0]=0, Value[1]=54321
I (XXX) MB_SLAVE: Sequential counter incremented to: 1
I (XXX) MB_SLAVE: AP timeout reached - shutting down WiFi AP
I (XXX) MB_SLAVE: WiFi AP stopped - device now running in Modbus-only mode
```

## Troubleshooting

### No Communication

1. **Check wiring:** 
   - TX (GPIO18) → HW-519 TXD
   - RX (GPIO16) → HW-519 RXD
   - 5V → HW-519 VCC (not 3.3V!)
   - GND → HW-519 GND
   - A/B to RS485 bus (try swapping if needed)
2. **Check termination:** HW-519 has built-in 120Ω termination - check jumper position
3. **Check baud rate:** Master and slave must use the same baud rate (9600)
4. **Check slave address:** Make sure the master is addressing slave ID 1
5. **Power:** HW-519 needs 5V, not 3.3V

### Garbled Data

1. **Check baud rate match:** Both devices must use 9600 baud
2. **Check ground connection:** Ensure common ground between devices
3. **Check HW-519 power:** Ensure HW-519 has proper 5V supply
4. **Check A/B wiring:** A to A, B to B (if reversed, swap them)

### ESP32-S3 Not Responding

1. **Check pins:** Verify GPIO pins match the configuration
2. **Check UART port:** UART1 is used (not UART0 which is typically used for console)
3. **Flash the firmware:** Run `pio run --target upload`

## Customization

### Change Slave Address

Edit in `main/main.c`:
```c
#define MB_SLAVE_ADDR   (1)     // Change to your desired address (1-247)
```

### Change Baud Rate

Edit in `main/main.c`:
```c
#define MB_DEV_SPEED    (9600)  // Change to 115200, 19200, etc.
```

### Change Pins

Edit in `main/main.c`:
```c
#define MB_UART_TXD     (18)    // TX pin - HW-519 TXD
#define MB_UART_RXD     (16)    // RX pin - HW-519 RXD
// RTS not used with HW-519 (automatic flow control)
```

### Add More Registers

Modify the `holding_reg_params_t` structure:
```c
typedef struct {
    uint16_t sequential_counter;
    uint16_t random_number;
    uint16_t new_register1;      // Add new registers
    uint16_t new_register2;
    // ... etc
} holding_reg_params_t;
```

Don't forget to update:
```c
#define MB_REG_HOLDING_SIZE     (4)  // Update count
```

## Security Considerations

### Development vs. Production

This code is intended for **development and testing** purposes. For production deployments, consider the following security enhancements:

### WiFi Configuration Portal

⚠️ **Current Implementation:**
- Hard-coded WiFi credentials (`modbus123`)
- No authentication on web interface
- Plain HTTP (no TLS)
- WiFi password is logged to console

✅ **Recommended for Production:**
1. Generate unique WiFi passwords per device (store in NVS)
2. Add authentication to the web interface (basic auth or token-based)
3. Reduce AP timeout to minimum needed (or enable via physical button)
4. Remove password from logs (line 448 in main.c)
5. Consider using HTTPS with a self-signed certificate
6. Add CSRF protection for configuration changes

### Modbus Protocol

⚠️ **Inherent Limitations:**
- Modbus RTU has no built-in authentication or encryption
- Any device on the RS485 bus can send/receive frames
- Register values are transmitted in plain text

✅ **Mitigation Strategies:**
1. **Physical Security:** Protect RS485 bus wiring
2. **Network Segmentation:** Isolate Modbus network from untrusted networks
3. **Application-Layer Security:** Consider adding custom authentication in unused registers
4. **Rate Limiting:** Monitor for abnormal request patterns
5. **Logging & Monitoring:** Track all configuration changes

### ESP32-S3 Hardware Security Features

For production firmware, enable these ESP-IDF security features:

```
CONFIG_SECURE_BOOT_V2_ENABLED=y          # Secure Boot V2
CONFIG_SECURE_FLASH_ENC_ENABLED=y        # Flash Encryption
CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=y      # Memory Protection
CONFIG_HEAP_POISONING_COMPREHENSIVE=y     # Heap Corruption Detection
CONFIG_FREERTOS_CHECK_STACKOVERFLOW=2     # Stack Overflow Detection
```

**Efuse Settings (one-way, test thoroughly first):**
- Disable JTAG debugging
- Disable USB Serial/JTAG (if not needed)
- Enable download mode protection

### Secure Coding Practices

✅ **Already Implemented:**
- Uses `snprintf` instead of `sprintf`
- Bounds checking on Modbus frame reception
- CRC validation on incoming frames
- Input validation on slave ID (1-247 range)

⚠️ **Additional Recommendations:**
1. Add rate limiting on HTTP endpoints
2. Validate all user inputs from web interface
3. Add Content Security Policy headers
4. Implement proper session management
5. Add mutex protection for shared register access

### Vulnerability Summary

| Risk Level | Issue | Mitigation |
|------------|-------|------------|
| HIGH | Unauthenticated web config | Add authentication |
| HIGH | Hard-coded WiFi password | Generate per-device |
| MEDIUM | No Modbus authentication | Physical security + monitoring |
| MEDIUM | Password logged to console | Remove or mask in logs |
| LOW | No HTTPS | Add self-signed cert |
| LOW | No CSRF protection | Add token validation |

For a detailed security review, see the analysis performed on this codebase.

## License

This project is based on the ESP-IDF Modbus example from Espressif Systems.

## References

- [ESP-IDF Modbus Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/modbus.html)
- [Espressif esp-modbus GitHub](https://github.com/espressif/esp-modbus)
- [Modbus Protocol Specification](https://modbus.org/specs.php)
- [HW-519 RS485 Module Information](https://www.google.com/search?q=HW-519+RS485+module)
