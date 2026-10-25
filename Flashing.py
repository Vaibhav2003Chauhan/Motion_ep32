import subprocess
import sys
import os
import re
import glob

def get_version():
    version_file = os.path.join("src", "config", "version.h")
    with open(version_file, "r") as f:
        match = re.search(r'#define\s+FW_VERSION_STR\s+"([^"]+)"', f.read())
        return match.group(1) if match else "0.0.0"

def list_firmware():
    fw_dir = "firmware"
    if not os.path.isdir(fw_dir):
        return []
    files = sorted(glob.glob(os.path.join(fw_dir, "*.bin")))
    return [f for f in files if os.path.getsize(f) < 4 * 1024 * 1024]

def select_firmware():
    fw_list = list_firmware()
    if not fw_list:
        default = os.path.join(".pio", "build", "nodemcu-32s", "firmware.bin")
        if os.path.exists(default):
            return default
        return None

    if len(fw_list) == 1:
        print("Found: {}".format(os.path.basename(fw_list[0])))
        return fw_list[0]

    print("\nAvailable firmware files:")
    print("-" * 50)
    for i, fw in enumerate(fw_list, 1):
        size_kb = os.path.getsize(fw) / 1024
        print("  {}. {} ({:.0f} KB)".format(i, os.path.basename(fw), size_kb))
    print("-" * 50)

    while True:
        try:
            choice = input("Select firmware (1-{}): ".format(len(fw_list)))
            idx = int(choice) - 1
            if 0 <= idx < len(fw_list):
                return fw_list[idx]
            print("Invalid choice.")
        except (ValueError, EOFError):
            print("Invalid input.")

def find_port():
    import serial.tools.list_ports
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if "CP210" in p.description or "CH340" in p.description or "USB" in p.description:
            return p.device
    return None

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        print("ERROR: No COM port found. Pass it as argument: python Flashing.py COM27")
        sys.exit(1)

    fw = select_firmware()
    if not fw:
        print("ERROR: No firmware binary found. Run 'pio run' first.")
        sys.exit(1)

    print("\nFlashing: {}".format(os.path.basename(fw)))
    print("Port: {}".format(port))

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32",
        "--port", port,
        "--baud", "460800",
        "write_flash",
        "-z",
        "0x10000", fw
    ]

    print("Running: {}".format(" ".join(cmd)))
    result = subprocess.run(cmd)

    if result.returncode == 0:
        print("\nFlash successful! Opening monitor...")
        monitor_cmd = [sys.executable, "-m", "serial.tools.miniterm", port, "115200"]
        try:
            subprocess.run(monitor_cmd)
        except KeyboardInterrupt:
            print("\nMonitor closed.")
    else:
        print("\nFlash FAILED with code {}".format(result.returncode))
        sys.exit(result.returncode)

if __name__ == "__main__":
    main()
