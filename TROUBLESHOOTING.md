# Modbus Troubleshooting Guide for HW-519

## Current Status: Connection Timeout

If you're seeing "Connection timed out" errors, follow these steps:

### 1. Verify ESP32 is Running
Check the monitor output. You should see:
```
I (XXX) MB_SLAVE: Modbus slave alive - waiting for requests (poll count: XXXX)
```

If you DON'T see this message every 10 seconds:
- The firmware didn't upload correctly
- The ESP32 is stuck in a crash loop
- Check for panic/error messages in the monitor

### 2. Check Hardware Connections

#### HW-519 Wiring (CRITICAL):
```
ESP32-S3          HW-519
GPIO18 (TX) -->   TXD
GPIO16 (RX) <--   RXD
5V          -->   VCC (NOT 3.3V!)
GND         -->   GND

HW-519            RS485 Bus
A           -->   A (Data+)
B           -->   B (Data-)
```

**Common Mistakes:**
1. ❌ Using 3.3V instead of 5V for HW-519 power
2. ❌ Swapped TX/RX (TX goes to TXD, RX goes to RXD)
3. ❌ A/B reversed (try swapping if no response)
4. ❌ Missing common ground

### 3. Verify HW-519 Module

The HW-519 has some configuration jumpers/options:

1. **Check the 120Ω termination resistor**
   - Look for a jumper labeled "120R" or similar
   - For testing with short cables, termination often NOT needed
   - For long cables (>10m), enable termination on BOTH ends

2. **Verify Power LED**
   - HW-519 should have a power indicator LED
   - If not lit, check 5V power supply

3. **Check for TX/RX LEDs**
   - Some HW-519 modules have activity LEDs
   - TX LED should flicker when master sends requests
   - If TX LED flickers but RX doesn't = ESP32 not responding

### 4. Test UART Communication

Create a simple loopback test by temporarily connecting GPIO18 to GPIO16 (short TX to RX on ESP32 side):

**WARNING:** Do this ONLY with HW-519 disconnected!

This will verify:
- UART is properly configured
- Pins are correct
- Basic communication works

### 5. Check Modbus Master Configuration

Your master MUST match these settings EXACTLY:

| Setting       | Value  | Notes                           |
|---------------|--------|---------------------------------|
| Mode          | RTU    | NOT ASCII, NOT TCP              |
| Baud Rate     | 9600   | Must match exactly              |
| Data Bits     | 8      | Standard                        |
| Parity        | None   | No parity (N)                   |
| Stop Bits     | 1      | Standard                        |
| Slave Address | 1      | Default (configurable via WiFi) |

### 6. Common mbpoll Issues

If using mbpoll, the correct command is:
```bash
# Read 10 holding registers starting at address 0
mbpoll -a 1 -0 -r 0 -c 10 -t 4 -b 9600 -P none -s 1 /dev/ttyUSB0

# Breakdown:
# -a 1       : Slave address 1
# -r 0       : Start at register 0
# -c 10      : Read 10 registers
# -t 4       : Type 4 = holding registers (16-bit)
# -b 9600    : Baud rate
# -P none    : No parity
# -s 1       : 1 stop bit
# /dev/ttyUSB0 : Your serial port
```

**Common mbpoll mistakes:**
- Missing `-s 1` (stop bits)
- Using `-t 3` instead of `-t 4` (wrong register type)
- Wrong serial port

### 7. Verify Serial Port Permissions

```bash
# Check if port exists
ls -la /dev/ttyUSB* /dev/ttyACM*

# Check permissions
ls -la /dev/ttyUSB0

# Add user to dialout group (Linux)
sudo usermod -a -G dialout $USER
# Then logout/login

# Or temporarily use sudo
sudo mbpoll -a 1 -r 0 -c 10 -t 4 -b 9600 -P none /dev/ttyUSB0
```

### 8. Try Different Serial Ports

USB-to-RS485 adapters can appear as different devices:
```bash
# List all serial devices
ls /dev/tty* | grep -E 'USB|ACM'

# Try dmesg to see what was detected
dmesg | tail -20
```

### 9. Raw UART Test

If nothing works, test raw UART communication:

```bash
# Install minicom
sudo apt-get install minicom

# Configure for 9600 8N1
minicom -b 9600 -D /dev/ttyUSB0

# Type some characters - you won't see Modbus response but 
# this verifies the serial port is working
```

### 10. Enable ESP32 Debug Logging

If you're getting "slave alive" messages but still timeout, there might be an issue with the Modbus stack initialization. Check for these errors in boot log:

```
E (XXX) MB_SLAVE: mb stack initialization failure
E (XXX) MB_SLAVE: mb stack set slave ID failure
```

### 11. A/B Polarity Test

RS485 A/B can be reversed (there's no universal standard):

```
Try 1: HW-519 A → Bus A, HW-519 B → Bus B
Try 2: HW-519 A → Bus B, HW-519 B → Bus A (SWAP)
```

One of these WILL work if everything else is correct.

### 12. Check for Bus Contention

If multiple devices on RS485 bus:
- Ensure only ONE device has termination enabled on each end
- Verify all devices use different slave addresses
- Check for ground loops (only one common ground point)

### 13. Voltage Levels

HW-519 needs:
- **VCC: 5V** (4.5V - 5.5V acceptable)
- ESP32 GPIO: 3.3V logic (HW-519 is compatible)

Use a multimeter to verify:
- VCC = ~5V
- GPIO18/16 when idle = ~3.3V

### 14. Last Resort: Oscilloscope/Logic Analyzer

If you have test equipment:
- Monitor GPIO18 (TX) - should see data bursts when master polls
- Monitor A/B differential voltage - should see ±2V to ±6V swings
- Verify baud rate is actually 9600 (104 μs per bit)

## Still Not Working?

Provide these details:
1. ESP32 monitor output (especially boot messages)
2. Exact mbpoll or test command used
3. USB-to-RS485 adapter model
4. Photos of wiring
5. Output of `ls -la /dev/ttyUSB*`

## Quick Test Checklist

- [ ] ESP32 boots and shows "Modbus slave alive" every 10 seconds
- [ ] HW-519 has 5V power (NOT 3.3V)
- [ ] GPIO18 → HW-519 TXD
- [ ] GPIO16 → HW-519 RXD  
- [ ] Common ground connected
- [ ] A/B connected to bus (tried both polarities)
- [ ] Master uses: 9600 baud, 8-N-1, RTU mode
- [ ] Master addresses slave ID 1
- [ ] Serial port has correct permissions
- [ ] No other program using the serial port
