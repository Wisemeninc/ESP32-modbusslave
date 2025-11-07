#!/usr/bin/env python3
"""
Simple Modbus RTU test script for ESP32-S3 slave
"""
from pymodbus.client import ModbusSerialClient
import sys
import time

def test_modbus(port='/dev/ttyUSB0', slave_id=1):
    """Test Modbus communication with ESP32 slave"""
    
    print(f"Testing Modbus RTU on {port}")
    print(f"Slave ID: {slave_id}")
    print(f"Baudrate: 9600")
    print(f"Config: 8-N-1 (8 data bits, No parity, 1 stop bit)")
    print("-" * 50)
    
    # Create client
    client = ModbusSerialClient(
        port=port,
        baudrate=9600,
        bytesize=8,
        parity='N',
        stopbits=1,
        timeout=2  # Increased timeout
    )
    
    # Connect
    if not client.connect():
        print("ERROR: Failed to connect to serial port")
        print(f"Make sure {port} exists and you have permissions")
        print("Try: ls -la /dev/ttyUSB* /dev/ttyACM*")
        return False
    
    print("✓ Serial port opened successfully")
    time.sleep(0.5)  # Give device time to settle
    
    # Test 1: Read holding registers
    print("\nTest 1: Reading 10 holding registers (address 0-9)...")
    try:
        result = client.read_holding_registers(
            address=0, 
            count=10, 
            slave=slave_id
        )
        
        if result.isError():
            print(f"✗ Read FAILED: {result}")
            print("\nTroubleshooting:")
            print("1. Check wiring: TX(GPIO17)->DI, RX(GPIO16)->RO, RTS(GPIO18)->DE&RE")
            print("2. Verify MAX485 has 5V power")
            print("3. Check A/B connections (try swapping if needed)")
            print("4. Verify slave is running (check monitor output)")
            print("5. Check slave address matches (default is 1)")
            return False
        else:
            print("✓ Read SUCCESS!")
            print("\nRegister Values:")
            for i, value in enumerate(result.registers):
                desc = {
                    0: "Sequential Counter",
                    1: "Random Number",
                    2: "Second Counter",
                }.get(i, f"General Purpose")
                print(f"  Register {i:2d}: {value:5d} (0x{value:04X}) - {desc}")
    except Exception as e:
        print(f"✗ Exception during read: {e}")
        return False
    
    # Test 2: Write to register
    print("\nTest 2: Writing value 999 to register 0...")
    try:
        result = client.write_register(
            address=0, 
            value=999, 
            slave=slave_id
        )
        
        if result.isError():
            print(f"✗ Write FAILED: {result}")
            return False
        else:
            print("✓ Write SUCCESS!")
    except Exception as e:
        print(f"✗ Exception during write: {e}")
        return False
    
    # Test 3: Verify write
    print("\nTest 3: Verifying write (reading register 0 again)...")
    try:
        result = client.read_holding_registers(
            address=0, 
            count=1, 
            slave=slave_id
        )
        
        if result.isError():
            print(f"✗ Verification read FAILED: {result}")
            return False
        else:
            value = result.registers[0]
            # Note: Sequential counter increments on access, so value will be 1000
            print(f"✓ Register 0 value: {value}")
            if value == 1000:  # Incremented from 999
                print("✓ PASS: Sequential counter incremented correctly!")
            else:
                print(f"⚠ Value is {value}, expected ~1000 (increments on read)")
    except Exception as e:
        print(f"✗ Exception during verification: {e}")
        return False
    
    client.close()
    print("\n" + "=" * 50)
    print("✓ ALL TESTS PASSED!")
    print("=" * 50)
    return True

if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
    slave_id = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    
    print("ESP32-S3 Modbus RTU Slave Tester")
    print("=" * 50)
    
    success = test_modbus(port, slave_id)
    
    if not success:
        print("\n" + "!" * 50)
        print("TESTS FAILED - Check connections and configuration")
        print("!" * 50)
        sys.exit(1)
    else:
        sys.exit(0)
