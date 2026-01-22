#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
from pathlib import Path


def run_cmd(cmd: list[str]) -> int:
    return subprocess.call(cmd)


def show_cards() -> None:
    cards_path = Path("/proc/asound/cards")
    if not cards_path.exists():
        print("/proc/asound/cards not found.")
        raise SystemExit(1)
    print(cards_path.read_text())


def list_devices() -> None:
    for tool in ("aplay", "arecord"):
        if shutil.which(tool):
            subprocess.call([tool, "-l"])
        else:
            print(f"{tool} not found.")


def setup_loopback(index: int | None, substreams: int | None) -> None:
    if os.geteuid() != 0:
        print("This command must be run as root (no sudo usage in tests).")
        raise SystemExit(1)
    args = ["modprobe", "snd-aloop"]
    if index is not None:
        args.append(f"index={index}")
    if substreams is not None:
        args.append(f"pcm_substreams={substreams}")
    raise SystemExit(run_cmd(args))


def teardown_loopback() -> None:
    if os.geteuid() != 0:
        print("This command must be run as root (no sudo usage in tests).")
        raise SystemExit(1)
    raise SystemExit(run_cmd(["modprobe", "-r", "snd-aloop"]))


def main() -> None:
    parser = argparse.ArgumentParser(description="Manage ALSA loopback module.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    setup = subparsers.add_parser("setup", help="Load snd-aloop module")
    setup.add_argument("--index", type=int, default=None)
    setup.add_argument("--pcm-substreams", type=int, default=None)

    subparsers.add_parser("teardown", help="Unload snd-aloop module")
    subparsers.add_parser("status", help="Show loopback card status")
    subparsers.add_parser("list", help="List ALSA playback/capture devices")

    args = parser.parse_args()

    if args.command == "setup":
        setup_loopback(args.index, args.pcm_substreams)
    if args.command == "teardown":
        teardown_loopback()
    if args.command == "status":
        show_cards()
        return
    if args.command == "list":
        list_devices()
        return


if __name__ == "__main__":
    main()
