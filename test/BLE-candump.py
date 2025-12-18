#!/usr/bin/env python3
import asyncio
import time
from bleak import BleakClient

# --- CONFIG ---
BLE_ADDR = "94:A9:90:37:D1:1E"
CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"
CAN_IFACE = "can0"
OUT_FILE = "BLE-candump.log"

rx_buffer = bytearray()


def parse_slcan_line(line: str):
    """
    Parse one SLCAN frame and return candump-style string
    """
    try:
        ts = time.time()

        if line[0] == 't':  # standard ID
            can_id = int(line[1:4], 16)
            dlc = int(line[4], 16)
            data = line[5:5 + dlc * 2]

        elif line[0] == 'T':  # extended ID
            can_id = int(line[1:9], 16)
            dlc = int(line[9], 16)
            data = line[10:10 + dlc * 2]

        else:
            return None

        bytes_out = " ".join(data[i:i + 2] for i in range(0, len(data), 2))
        return f"({ts:.6f}) {CAN_IFACE} {can_id:08X}#{bytes_out}"

    except Exception:
        return None


def on_notify(sender: int, data: bytearray):
    global rx_buffer
    rx_buffer.extend(data)

    while b'\r' in rx_buffer:
        line, _, rx_buffer = rx_buffer.partition(b'\r')
        try:
            text = line.decode(errors="ignore")
        except Exception:
            continue

        candump = parse_slcan_line(text)
        if candump:
            print(candump)
            with open(OUT_FILE, "a") as f:
                f.write(candump + "\n")


async def main():
    print(f"Connecting to {BLE_ADDR} …")
    async with BleakClient(BLE_ADDR) as client:
        if not client.is_connected:
            print("Failed to connect")
            return

        print("Connected")
        print("Logging to", OUT_FILE)
        await client.start_notify(CHAR_UUID, on_notify)

        try:
            while True:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            print("\nStopping…")
            await client.stop_notify(CHAR_UUID)


asyncio.run(main())
