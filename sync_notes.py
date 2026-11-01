import argparse
import hashlib
import os
import re
import time
from pathlib import Path

import serial

BAUD = 115200
TIMEOUT = 10
CHUNK = 512


def read_line(ser):
    line = ser.readline().decode("ascii", errors="replace").strip()
    if not line:
        raise TimeoutError("Timed out waiting for ESP32")
    return line


def crc32_file(path):
    import zlib
    crc = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(CHUNK), b""):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def remote_list(ser):
    ser.write(b"LIST\n")
    files = {}

    while True:
        line = read_line(ser)
        if line == "END":
            return files
        if line.startswith("ERR"):
            raise RuntimeError(line)

        match = re.fullmatch(
            r"FILE ([^ ]+) (\d+) ([0-9A-Fa-f]{8})", line
        )
        if match:
            name, size, crc = match.groups()
            files[name] = (int(size), int(crc, 16))


def upload(ser, path, name):
    size = path.stat().st_size
    crc = crc32_file(path)

    ser.write(f"PUT {name} {size} {crc:08X}\n".encode())
    response = read_line(ser)
    if response != "GO":
        raise RuntimeError(f"Upload rejected: {response}")

    with open(path, "rb") as f:
        while True:
            chunk = f.read(CHUNK)
            if not chunk:
                break
            ser.write(chunk)

    response = read_line(ser)
    if response != "OK":
        raise RuntimeError(f"Upload failed: {response}")

    print(f"Uploaded {name}")


def download(ser, name, destination):
    ser.write(f"GET {name}\n".encode())
    header = read_line(ser)

    match = re.fullmatch(r"DATA (\d+) ([0-9A-Fa-f]{8})", header)
    if not match:
        raise RuntimeError(f"Download failed: {header}")

    size = int(match.group(1))
    expected_crc = int(match.group(2), 16)

    temp = destination.with_suffix(destination.suffix + ".part")
    crc = 0
    remaining = size

    import zlib
    with open(temp, "wb") as f:
        while remaining:
            chunk = ser.read(min(CHUNK, remaining))
            if not chunk:
                raise TimeoutError("Incomplete download")
            f.write(chunk)
            crc = zlib.crc32(chunk, crc)
            remaining -= len(chunk)

    if (crc & 0xFFFFFFFF) != expected_crc:
        temp.unlink(missing_ok=True)
        raise RuntimeError(f"CRC mismatch downloading {name}")

    temp.replace(destination)
    print(f"Downloaded {name}")


def safe_name(name):
    return (
        name not in ("", ".", "..")
        and "/" not in name
        and "\\" not in name
        and ":" not in name
    )


def sync(port, folder):
    folder.mkdir(parents=True, exist_ok=True)

    with serial.Serial(port, BAUD, timeout=TIMEOUT) as ser:
        time.sleep(2)
        ser.reset_input_buffer()

        # Get files currently on the ESP32
        ser.write(b"LIST\n")
        remote = {}

        while True:
            line = read_line(ser)
            if line == "END":
                break

            match = re.fullmatch(
                r"FILE ([^ ]+) (\d+) ([0-9A-Fa-f]{8})", line
            )
            if match:
                name, size, crc = match.groups()
                remote[name] = (int(size), int(crc, 16))

        # Get local files
        local = {
            p.name: (p.stat().st_size, crc32_file(p))
            for p in folder.iterdir()
            if p.is_file()
            and safe_name(p.name)
            and not p.name.endswith(".part")
        }

        # Upload new and changed files
        for name, info in sorted(local.items()):
            if remote.get(name) != info:
                upload(ser, folder / name, name)

        # Delete remote files absent from the local folder
        for name in sorted(set(remote) - set(local)):
            ser.write(f"DELETE {name}\n".encode())
            response = read_line(ser)

            if response != "OK":
                raise RuntimeError(
                    f"Failed to delete remote file {name}: {response}"
                )

            print(f"Deleted remote file: {name}")

        ser.write(b"FINISH\n")
        response = read_line(ser)

        if response != "ACKFINISH":
            raise RuntimeError(
                f"esp32 did not end with ACKFINISH, instead: {response}"
            )

        print("Sync complete.")



if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="Serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("folder", type=Path, help="Local notes directory")
    args = parser.parse_args()

    sync(args.port, args.folder)
