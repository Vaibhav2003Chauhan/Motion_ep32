Import("env")
import shutil
import os
import re

def copy_firmware(source, target, env):
    version_file = os.path.join(env["PROJECT_SRC_DIR"], "config", "version.h")
    version = "0.0.0"

    with open(version_file, "r") as f:
        content = f.read()
        match = re.search(r'#define\s+FW_VERSION_STR\s+"([^"]+)"', content)
        if match:
            version = match.group(1)

    fw_dir = os.path.join(env["PROJECT_DIR"], "firmware")
    os.makedirs(fw_dir, exist_ok=True)

    src_bin = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    dest_bin = os.path.join(fw_dir, "LIS3DH_v{}.bin".format(version))

    if not os.path.exists(src_bin):
        print("WARNING: {} not found, skipping copy".format(src_bin))
        return

    shutil.copy2(src_bin, dest_bin)
    print("Firmware copied: {} ({} bytes)".format(dest_bin, os.path.getsize(dest_bin)))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware)
