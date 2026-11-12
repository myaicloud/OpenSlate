#!/usr/bin/env python3
"""Build the firmware files for the OpenSlate web flasher (site/).

Runs the two release builds, merges bootloader + partition table + boot_app0
+ app into one image per chip (flashed at offset 0 by ESP Web Tools), copies
the app-only images for over-the-air updates from the web app, and writes the
firmware version from src/config.h into manifest.json and index.html.

Usage (from the repository root):  python site/build.py
Needs PlatformIO; esptool is taken from PlatformIO's tool-esptool package.
"""

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
OUT = SITE / "firmware"
PIO_HOME = Path.home() / ".platformio"

# env, esptool chip name, bootloader offset, output stem
TARGETS = [
    ("esp32c3_release", "esp32c3", "0x0", "openslate-esp32c3"),
    ("esp32dev_release", "esp32", "0x1000", "openslate-esp32"),
]


def find_tool(*candidates):
    for c in candidates:
        if c and Path(c).exists():
            return str(c)
        found = shutil.which(str(c)) if c else None
        if found:
            return found
    sys.exit(f"not found: {candidates}")


def main():
    pio = find_tool(shutil.which("pio"), PIO_HOME / "penv" / "Scripts" / "pio.exe",
                    PIO_HOME / "penv" / "bin" / "pio")
    esptool = find_tool(PIO_HOME / "packages" / "tool-esptool" / "esptool.exe",
                        PIO_HOME / "packages" / "tool-esptool" / "esptool",
                        shutil.which("esptool"), shutil.which("esptool.py"))
    boot_app0 = (PIO_HOME / "packages" / "framework-arduinoespressif32" / "tools"
                 / "partitions" / "boot_app0.bin")
    if not boot_app0.exists():
        sys.exit(f"missing {boot_app0}")

    version = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"',
                        (ROOT / "src" / "config.h").read_text(encoding="utf-8")).group(1)

    subprocess.run([pio, "run", *sum((["-e", t[0]] for t in TARGETS), [])],
                   cwd=ROOT, check=True)

    OUT.mkdir(exist_ok=True)
    for env, chip, boot_off, stem in TARGETS:
        build = ROOT / ".pio" / "build" / env
        subprocess.run([esptool, "--chip", chip, "merge-bin",
                        "-o", str(OUT / f"{stem}.bin"),
                        "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep",
                        boot_off, str(build / "bootloader.bin"),
                        "0x8000", str(build / "partitions.bin"),
                        "0xe000", str(boot_app0),
                        "0x10000", str(build / "firmware.bin")], check=True)
        shutil.copyfile(build / "firmware.bin", OUT / f"{stem}-update.bin")

    manifest_path = SITE / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["version"] = version
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    index = SITE / "index.html"
    html = index.read_text(encoding="utf-8")
    html = re.sub(r'(<span class="mono" id="fwver">)[^<]*(</span>)', rf"\g<1>{version}\g<2>", html)
    index.write_text(html, encoding="utf-8")

    print(f"OpenSlate {version}: web flasher files written to {OUT}")


if __name__ == "__main__":
    main()
