import hashlib
import os
from pathlib import Path
import time
import requests

ESP32_URL = "http://192.168.1.251"
SOURCE_DIR = Path(r"/home/cheeto/gcse/")

# Set True to delete SD card files that aren't in SOURCE_DIR.
DELETE_EXTRAS = False

CHUNK_SIZE = 1024 * 1024

def connect_to_esp32(session):
    while True:
        try:
            response = session.get(
                f"{ESP32_URL}/manifest",
                timeout=5
            )
            response.raise_for_status()

            print("ESP32 found!")
            return {
                item["path"]: item
                for item in response.json()["files"]
            }

        except (requests.RequestException, ValueError) as e:
            print(f"ESP32 not available ({e}). Retrying in 5 seconds...")
            time.sleep(1)

def sha256_file(path):
    digest = hashlib.sha256()

    with open(path, "rb") as f:
        while chunk := f.read(CHUNK_SIZE):
            digest.update(chunk)

    return digest.hexdigest()


def get_remote_manifest(session):
    response = session.get(
        f"{ESP32_URL}/manifest", timeout=120
    )
    response.raise_for_status()

    return {
        item["path"]: item
        for item in response.json()["files"]
    }


def upload_file(session, local_path, relative_path):
    print(f"Uploading: {relative_path}")

    with open(local_path, "rb") as f:
        response = session.post(
            f"{ESP32_URL}/upload",
            params={"path": relative_path},
            files={"file": (local_path.name, f)},
            timeout=(10, 300),
        )

    response.raise_for_status()


def delete_remote_file(session, relative_path):
    response = session.post(
        f"{ESP32_URL}/delete",
        params={"path": relative_path},
        timeout=30,
    )

    response.raise_for_status()
    print(f"Deleted: {relative_path}")


def main():
    if not SOURCE_DIR.is_dir():
        raise RuntimeError(f"Folder not found: {SOURCE_DIR}")

    # Use forward slashes in relative paths, including on Windows.
    local_files = {}

    for path in SOURCE_DIR.rglob("*"):
        if path.is_symlink():
            continue

        if path.is_file():
            relative = path.relative_to(SOURCE_DIR).as_posix()
            local_files[relative] = path

    with requests.Session() as session:
        remote_files = connect_to_esp32(session)

        uploaded = 0
        unchanged = 0

        for relative, local_path in local_files.items():
            local_size = local_path.stat().st_size
            remote = remote_files.get(relative)

            # Skip hashing the local file if its size differs,
            # otherwise compare hashes to detect same-size changes.
            if remote and remote["size"] == local_size:
                local_hash = sha256_file(local_path)

                if local_hash == remote["sha256"]:
                    unchanged += 1
                    continue

            upload_file(session, local_path, relative)
            uploaded += 1

        deleted = 0

        if DELETE_EXTRAS:
            for relative in remote_files:
                if relative not in local_files:
                    delete_remote_file(session, relative)
                    deleted += 1

    print(
        f"Sync complete: {uploaded} uploaded, "
        f"{unchanged} unchanged, {deleted} deleted."
    )


if __name__ == "__main__":
    main()