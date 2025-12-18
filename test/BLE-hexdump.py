#!/usr/bin/env python3
import asyncio
from bleak import BleakClient

# --- CONFIG ---
BLE_ADDR = "94:A9:90:37:D1:1E"   # device MAC
CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"  # FFE1 (HM-10 style)
# For Nordic UART RX instead, use:
# CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

def on_notify(sender: int, data: bytearray):
    hexstr = " ".join(f"{b:02X}" for b in data)
    print(hexstr)

async def main():
    print(f"Connecting to {BLE_ADDR} …")
    async with BleakClient(BLE_ADDR) as client:
        if not client.is_connected:
            print("Failed to connect")
            return

        print("Connected")
        print("Subscribing to notifications…")
        await client.start_notify(CHAR_UUID, on_notify)

        print("Receiving data (Ctrl+C to stop)…")
        try:
            while True:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            print("\nStopping…")
            await client.stop_notify(CHAR_UUID)

asyncio.run(main())
