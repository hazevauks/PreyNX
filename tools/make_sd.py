#!/usr/bin/env python3
"""Fills SD_CARD/ with a test build: copy its contents to the root of the SD card.

    python tools/make_sd.py [--base <folder with the .pk4 files>] [--shaders <folder>]

The game's files are linked, not copied, when the folder is on the same drive.
"""

import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def place(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if target.stat().st_size == source.stat().st_size and target.stat().st_mtime >= source.stat().st_mtime:
            return
        target.unlink()
    try:
        os.link(source, target)
    except OSError:
        shutil.copy2(source, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nro", default=str(ROOT / "build" / "prey.nro"))
    parser.add_argument("--base", help="folder with the retail .pk4 files")
    parser.add_argument("--shaders", help="folder with the .dksh files from tools/build_shaders.py")
    args = parser.parse_args()

    app = ROOT / "SD_CARD" / "switch" / "prey"
    nro = pathlib.Path(args.nro)
    if not nro.is_file():
        raise SystemExit("%s was not found: build it first" % nro)

    app.mkdir(parents=True, exist_ok=True)
    shutil.copy2(nro, app / "prey.nro")
    print("prey.nro: %.1f MB" % (nro.stat().st_size / (1024 * 1024)))

    if args.base:
        archives = sorted(pathlib.Path(args.base).glob("*.pk4"))
        for archive in archives:
            if not archive.name.lower().startswith("game"):
                place(archive, app / "base" / archive.name.lower())
        print("base: %d archives" % len(list((app / "base").glob("*.pk4"))))

    if args.shaders:
        shaders = sorted(pathlib.Path(args.shaders).glob("*.dksh"))
        for shader in shaders:
            place(shader, app / "shaders" / shader.name)
        print("shaders: %d files" % len(shaders))

    total = sum(f.stat().st_size for f in app.rglob("*") if f.is_file())
    print("SD_CARD/switch/prey: %.2f GB" % (total / (1024 ** 3)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
