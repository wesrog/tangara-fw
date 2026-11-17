#!/usr/bin/env python
# Copyright 2026 Tangara contributors
#
# SPDX-License-Identifier: GPL-3.0-only
"""
Flash only the parts of a Tangara build that differ from what is on the device.

`idf.py flash` writes every image in the build (bootloader, app, collation
data, the lua and repl filesystems): about 7.5 MB, even when one Lua file
changed. This script instead:

  1. Builds the project (unless --no-build).
  2. Connects to the device and asks it for the MD5 of every 64 KiB chunk of
     every image listed in build/flasher_args.json.
  3. Writes only the chunks whose MD5 differs, using a normal `esptool
     write_flash` run (so esptool's own verification and retry still apply).

There is no cache of "what was flashed last time": the comparison is always
against what is actually on the device's flash, so it is safe to mix this with
plain `idf.py flash`, other checkouts, or other machines. If anything goes
wrong while checking, it writes every image in full, like `idf.py flash`.

Run from the repo root with the ESP-IDF environment loaded (`. ./.env`):

    tools/quickflash.py -p /dev/serial/by-id/usb-cool_tech_zone_Tangara_*

Options: --full to skip the comparison, --no-build, -b to change the baud rate.
"""

import argparse
import glob
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time

import esptool
from esptool import cmds
from esptool.util import pad_to

CHUNK = 64 * 1024
DEFAULT_BAUD = 1500000


def log(msg):
    print(f"[quickflash] {msg}", flush=True)


def load_images(build_dir):
    with open(os.path.join(build_dir, "flasher_args.json")) as f:
        fa = json.load(f)
    images = []
    for addr, rel in fa["flash_files"].items():
        path = os.path.normpath(os.path.join(build_dir, rel))
        with open(path, "rb") as f:
            data = f.read()
        # esptool pads every image to a multiple of 4 bytes with 0xFF.
        images.append((int(addr, 0), path, pad_to(data, 4)))
    images.sort()
    settings = fa["flash_settings"]
    chip = fa.get("extra_esptool_args", {}).get("chip", "esp32")
    return images, settings, chip


def expected_bytes(esp, addr, data, settings, flash_size, chip):
    """What esptool would actually put on flash for this image.

    write_flash rewrites the flash mode/freq/size bytes in the bootloader
    header (and its SHA digest), so the bootloader on flash never matches the
    file byte for byte. Apply the same transformation before comparing.
    """
    if addr != esp.BOOTLOADER_FLASH_OFFSET:
        return data
    ns = argparse.Namespace(
        flash_mode=settings["flash_mode"],
        flash_freq=settings["flash_freq"],
        flash_size=flash_size,
        chip=chip,
    )
    return cmds._update_image_flash_params(esp, addr, ns, data)


def other_flashers():
    """Other processes that look like they're using the device (esptool,
    idf.py flash/monitor, serial captures). Two flashers at once corrupt the
    write, so refuse to start while one is running."""
    me = {os.getpid(), os.getppid()}
    found = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit() or int(pid) in me:
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode(errors="replace").strip()
        except OSError:
            continue
        if "quickflash" in cmd:
            continue
        if ("esptool" in cmd or
                ("idf.py" in cmd and any(w in cmd.split() for w in ("flash", "app-flash", "monitor"))) or
                ("ttyACM" in cmd or "cool_tech_zone_Tangara" in cmd)):
            found.append(f"{pid}: {cmd[:120]}")
    return found


def connect(port):
    """Opens the port, retrying briefly: right after a reset the port can
    report busy for a second or two."""
    for attempt in range(5):
        try:
            return cmds.detect_chip(port, esptool.loader.ESPLoader.ESP_ROM_BAUD)
        except Exception as e:
            if attempt == 4:
                raise
            log(f"couldn't open {port} ({e}); retrying")
            time.sleep(2)


def find_dirty_regions(port, baud, images, settings, chip):
    """Returns [(addr, bytes)] that need writing, by asking the device."""
    t0 = time.time()
    esp = connect(port)
    try:
        esp = esp.run_stub()
        if baud > esp._port.baudrate:
            esp.change_baud(baud)
        flash_size = cmds.detect_flash_size(esp)
        if flash_size is None:
            raise RuntimeError("could not detect flash size")
        esp.flash_set_parameters(esptool.util.flash_size_bytes(flash_size))

        regions = []
        for addr, path, orig in images:
            data = expected_bytes(esp, addr, orig, settings, flash_size, chip)
            dirty = []
            for off in range(0, len(data), CHUNK):
                piece = data[off : off + CHUNK]
                ours = hashlib.md5(piece).hexdigest()
                theirs = esp.flash_md5sum(addr + off, len(piece))
                dirty.append(ours != theirs)
            # Merge runs of changed chunks into single writes.
            n_dirty = sum(dirty)
            off = 0
            while off < len(dirty):
                if not dirty[off]:
                    off += 1
                    continue
                end = off
                while end < len(dirty) and dirty[end]:
                    end += 1
                start_b, end_b = off * CHUNK, min(end * CHUNK, len(data))
                # Write from the original file bytes: esptool redoes the
                # bootloader header fixup itself (the bootloader is < 64 KiB,
                # so it is always written whole from its start).
                regions.append((addr + start_b, orig[start_b:end_b]))
                off = end
            name = os.path.relpath(path)
            if n_dirty == 0:
                log(f"  unchanged  {name}")
            else:
                log(f"  {n_dirty:3}/{len(dirty):<3} chunks changed  {name}")
        log(f"checked device in {time.time() - t0:.1f}s")
        return regions
    finally:
        # Leave the chip in the stub; the write step resets it again.
        esp._port.close()


def write_regions(port, baud, regions, settings, chip, after="hard_reset"):
    with tempfile.TemporaryDirectory(prefix="quickflash-") as tmp:
        pairs = []
        for i, (addr, data) in enumerate(regions):
            p = os.path.join(tmp, f"{i:02}-{addr:08x}.bin")
            with open(p, "wb") as f:
                f.write(data)
            pairs += [hex(addr), p]
        argv = [
            "--chip", chip, "-p", port, "-b", str(baud),
            "--before", "default_reset", "--after", after,
            "write_flash",
            "--flash_mode", settings["flash_mode"],
            "--flash_freq", settings["flash_freq"],
            "--flash_size", settings["flash_size"],
        ] + pairs
        env = dict(os.environ, ESPTOOL_OPEN_PORT_ATTEMPTS="5")
        subprocess.run([sys.executable, "-m", "esptool"] + argv, check=True, env=env)


def reset_only(port, chip):
    esp = connect(port)
    try:
        esp.hard_reset()
    finally:
        esp._port.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-p", "--port", help="serial port (default: auto-detect Tangara)")
    ap.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("-B", "--build-dir", default="build")
    ap.add_argument("--no-build", action="store_true", help="don't run idf.py build first")
    ap.add_argument("--full", action="store_true", help="write every image, like idf.py flash")
    args = ap.parse_args()

    port = args.port
    if port is None:
        found = glob.glob("/dev/serial/by-id/usb-cool_tech_zone_Tangara_*")
        if len(found) != 1:
            sys.exit("quickflash: can't find exactly one Tangara; pass -p PORT")
        port = found[0]

    busy = other_flashers()
    if busy:
        sys.exit("quickflash: something else seems to be using the device; "
                 "wait for it to finish:\n  " + "\n  ".join(busy))

    t_start = time.time()
    if not args.no_build:
        subprocess.run(["idf.py", "-B", args.build_dir, "build"], check=True)
    t_built = time.time()

    images, settings, chip = load_images(args.build_dir)
    full = [(a, d) for a, _, d in images]

    if args.full:
        regions = full
    else:
        try:
            regions = find_dirty_regions(port, args.baud, images, settings, chip)
        except Exception as e:  # anything odd: be safe and write everything
            log(f"couldn't compare with the device ({e!r}); writing everything")
            regions = full

    if regions:
        total = sum(len(d) for _, d in regions)
        log(f"writing {total / 1024:.0f} KiB in {len(regions)} region(s)")
        write_regions(port, args.baud, regions, settings, chip)
    else:
        log("device already matches this build; just resetting it")
        reset_only(port, chip)

    t_end = time.time()
    log(f"done: build {t_built - t_start:.1f}s, flash {t_end - t_built:.1f}s")


if __name__ == "__main__":
    main()
