#!/usr/bin/env python3
import can
import time

# --- CONFIG --- C
CAN_IFACE = "can0"
OUT_FILE = "CAN-candump.log"

# XCSoar CANaerospace whitelist, keep in sync with src/whitelist.h
WHITELIST = {
    300, 301, 302,
    315, 316, 319,
    321, 322, 326,
    333, 334, 354, 335,
    1036, 1037, 1038, 1039, 1040,
    1200,
    1300, 1301, 1302, 1303, 1304,
    1305,
    1510, 1518, 1519,
}


def is_whitelisted_id(can_id: int) -> bool:
    return can_id in WHITELIST


def format_candump(msg: can.Message) -> str:
    ts = msg.timestamp
    can_id = msg.arbitration_id
    data = " ".join(f"{b:02X}" for b in msg.data)
    return f"({ts:.6f}) {CAN_IFACE} {can_id:08X}#{data}"


def main():
    print(f"Opening SocketCAN interface {CAN_IFACE}")
    bus = can.interface.Bus(
        channel=CAN_IFACE,
        interface="socketcan",
)


    print(f"Logging whitelisted CAN frames to {OUT_FILE}")

    with open(OUT_FILE, "a") as logfile:
        try:
            for msg in bus:
                if msg.is_error_frame or msg.is_remote_frame:
                    continue

                can_id = msg.arbitration_id

                # CANaerospace uses standard 11-bit IDs
                if can_id > 0x7FF:
                    continue

                if not is_whitelisted_id(can_id):
                    continue

                line = format_candump(msg)
                print(line)
                logfile.write(line + "\n")
                logfile.flush()

        except KeyboardInterrupt:
            print("\nStopping…")


if __name__ == "__main__":
    main()
