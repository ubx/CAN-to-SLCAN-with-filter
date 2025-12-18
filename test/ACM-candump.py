#!/usr/bin/env python3
import serial
import time
import glob
import sys

# --- CONFIG ---
BAUDRATE = 576000  # typical for SLCAN over USB CDC
CAN_IFACE = "can0"
OUT_FILE = "ACM-candump.log"


def detect_acm_device() -> str | None:
    """
    Detect first available /dev/ttyACM* device
    """
    devices = sorted(glob.glob("/dev/ttyACM*"))
    if not devices:
        return None

    for dev in devices:
        try:
            ser = serial.Serial(
                dev,
                BAUDRATE,
                timeout=1,
                rtscts=False,
                dsrdtr=False,
            )
            ser.close()
            return dev
        except Exception:
            continue

    return None


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


def main():
    dev = detect_acm_device()
    if not dev:
        print("❌ No /dev/ttyACM* device found")
        sys.exit(1)

    print(f"Opening {dev} @ {BAUDRATE} baud")
    ser = serial.Serial(
        dev,
        BAUDRATE,
        timeout=1,
        rtscts=False,
        dsrdtr=False,
    )

    print("Logging to", OUT_FILE)

    with open(OUT_FILE, "a") as logfile:
        buffer = b""

        try:
            while True:
                data = ser.read(256)
                if not data:
                    continue

                buffer += data

                while b"\r" in buffer:
                    line, _, buffer = buffer.partition(b"\r")
                    text = line.decode(errors="ignore")

                    candump = parse_slcan_line(text)
                    if candump:
                        print(candump)
                        logfile.write(candump + "\n")
                        logfile.flush()

        except KeyboardInterrupt:
            print("\nStopping…")
        finally:
            ser.close()


if __name__ == "__main__":
    main()
