import os
import time

# PySerial is available in PlatformIO environment
try:
    from serial.tools import list_ports
except Exception:
    list_ports = None

# Try to get the PlatformIO/SCons build environment when this script is loaded
try:
    # When executed by PlatformIO, SCons provides Import to fetch the env
    from SCons.Script import Import  # type: ignore

    Import("env")  # noqa: F401 - provided by PlatformIO at build time
except Exception:  # pragma: no cover - not available outside PIO
    env = None  # type: ignore


def _ensure_sdkdefaults(env):
    sdk = os.path.join(env["PROJECT_DIR"], "sdkconfig.defaults")
    if not os.path.exists(sdk):
        with open(sdk, "w") as f:
            f.write(
                """
                CONFIG_TINYUSB_ENABLED=y
                CONFIG_TINYUSB_CDC_ENABLED=y
                CONFIG_USB_DEVICE_ENABLED=y
                CONFIG_USB_OTG_SUPPORTED=y
                CONFIG_TWAI_ISR_IN_IRAM=y
                """
            )


def _detect_ports():
    """Return a tuple (upload_port, cdc_port) if detectable; values may be None."""
    upload_port = None
    cdc_port = None
    if list_ports is None:
        return upload_port, cdc_port

    # Collect candidates once
    ports = list(list_ports.comports())
    # Prefer stable /dev/serial/by-id paths when available
    def _best_device_name(p):
        dev = getattr(p, "device", None)
        by_id = None
        try:
            # p.usb_info() may expose a unique serial, but the OS presents a by-id symlink
            base = "/dev/serial/by-id"
            if os.path.isdir(base):
                # pick any symlink that ends with the basename of this device
                for name in os.listdir(base):
                    link = os.path.join(base, name)
                    try:
                        if os.path.islink(link) and os.path.realpath(link) == dev:
                            by_id = link
                            break
                    except Exception:
                        continue
        except Exception:
            pass
        return by_id or dev
    for p in ports:
        vid = getattr(p, "vid", None)
        pid = getattr(p, "pid", None)
        product = (getattr(p, "product", None) or "").lower()
        description = (getattr(p, "description", None) or "").lower()
        interface = (getattr(p, "interface", None) or "").lower()

        # Espressif devices typically have VID 0x303A
        if vid != 0x303A:
            continue

        # USB-Serial-JTAG often contains 'jtag' in product/description
        if ("jtag" in product) or ("jtag" in description):
            upload_port = upload_port or _best_device_name(p)
            continue

        # TinyUSB CDC often reports CDC/ACM in interface/description/product
        if ("cdc" in product) or ("acm" in description) or ("cdc" in description) or ("cdc" in interface):
            cdc_port = cdc_port or _best_device_name(p)
            continue

        # If PID is in 0x4000 range, treat as app-defined TinyUSB device
        if pid and (0x4000 <= pid <= 0x4FFF):
            cdc_port = cdc_port or _best_device_name(p)

    # Fallback: if only one 303A device, assume it's upload port
    if not upload_port:
        for p in ports:
            if getattr(p, "vid", None) == 0x303A:
                upload_port = _best_device_name(p)
                break

    # Fallback: pick a second 303A device different from upload as CDC
    if not cdc_port and upload_port:
        for p in ports:
            devname = _best_device_name(p)
            if getattr(p, "vid", None) == 0x303A and devname != upload_port:
                cdc_port = devname
                break

    return upload_port, cdc_port


def _wait_for_cdc(timeout_s=12):
    """Wait for TinyUSB CDC port to appear after upload. Returns port or None."""
    end = time.time() + timeout_s
    last = None
    while time.time() < end:
        _, cdc = _detect_ports()
        if cdc:
            return cdc
        time.sleep(0.5)
    return last


def _pre_monitor_action(source, target, env):
    # If user already set monitor_port in platformio.ini, respect it
    try:
        user_mp = env.GetProjectOption("monitor_port")
    except Exception:
        user_mp = None

    if user_mp:
        return

    # Wait a bit for CDC device to enumerate after upload
    cdc = _wait_for_cdc(timeout_s=12)
    if cdc:
        env.Replace(MONITOR_PORT=cdc)
        print("[auto] monitor_port set to CDC port:", cdc)
    else:
        # As last resort, do nothing; PlatformIO will use default
        print("[auto] CDC port not found; using default monitor settings")


def _pre_upload_action(source, target, env):
    # If user already set upload_port in platformio.ini, respect it
    try:
        user_up = env.GetProjectOption("upload_port")
    except Exception:
        user_up = None

    if user_up:
        return

    up, _ = _detect_ports()
    if up:
        env.Replace(UPLOAD_PORT=up)
        print("[auto] upload_port set to USB-Serial-JTAG:", up)


def before_config(pioenv):
    # Keep legacy hook to ensure sdkconfig.defaults exists
    _ensure_sdkdefaults(pioenv)

    # Also attach dynamic port selection hooks if env is available
    try:
        pioenv.AddPreAction("upload", _pre_upload_action)
        pioenv.AddPreAction("monitor", _pre_monitor_action)
    except Exception:
        pass

# If PlatformIO provided a build env at import time, register actions now
if 'env' in globals() and env is not None:
    try:
        _ensure_sdkdefaults(env)
        env.AddPreAction("upload", _pre_upload_action)
        env.AddPreAction("monitor", _pre_monitor_action)
        print("[auto] add_config.py: dynamic port selection enabled")
    except Exception:
        # Safe to ignore when not under PIO/SCons
        pass

