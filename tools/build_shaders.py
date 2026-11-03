#!/usr/bin/env python3
"""Compiles the game's material programs for deko3D, from your own copy of Prey.

Reads glprogs/ from the retail .pk4 files, translates each program to GLSL
(tools/arb2glsl.py) and compiles it with devkitPro's uam. The results go to
<output>/<program>.<vert|frag>.dksh, which the game reads from
sdmc:/switch/prey/shaders/. They come from the game's data, so they stay out of
the repository.

    python tools/build_shaders.py <base folder with the .pk4 files> <output folder>
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arb2glsl

# the renderer's own versions in shaders/ cover the first ones, and the rest are for
# graphics cards from 2002
SKIPPED = {
    "interaction", "environment", "bumpyenvironment", "shadow", "test",
    "r200_interaction", "nv20_bumpandlight", "nv20_diffusecolor", "nv20_specularcolor", "nv20_diffuseandspecularcolor",
}


def find_uam(explicit):
    candidates = [explicit, shutil.which("uam")]
    devkitpro = os.environ.get("DEVKITPRO", "")
    if devkitpro:
        candidates.append(os.path.join(devkitpro, "tools", "bin", "uam.exe"))
        candidates.append(os.path.join(devkitpro, "tools", "bin", "uam"))
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    raise SystemExit("uam was not found: pass --uam, set DEVKITPRO, or add devkitPro's tools/bin to the PATH")


def read_programs(base):
    """Later archives override earlier ones, as in the game."""
    programs = {}
    for pk4 in sorted(pathlib.Path(base).glob("*.pk4")):
        with zipfile.ZipFile(pk4) as archive:
            for name in archive.namelist():
                normalized = name.replace("\\", "/").lower()
                if normalized.startswith("glprogs/") and normalized.rsplit(".", 1)[-1] in ("vfp", "vp", "fp"):
                    stem = normalized[len("glprogs/"):].rsplit(".", 1)[0]
                    programs[stem] = archive.read(name).decode("latin-1")
    return programs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("base", help="folder with the retail .pk4 files")
    parser.add_argument("output", help="folder for the .dksh files")
    parser.add_argument("--uam", help="path to devkitPro's uam")
    args = parser.parse_args()

    uam = find_uam(args.uam)
    programs = read_programs(args.base)
    if not programs:
        raise SystemExit("no glprogs found in " + args.base)
    os.makedirs(args.output, exist_ok=True)

    compiled = 0
    failed = []
    with tempfile.TemporaryDirectory() as work:
        for name, text in sorted(programs.items()):
            if name in SKIPPED:
                continue
            try:
                vertex, fragment = arb2glsl.translate_file(text)
            except arb2glsl.TranslationError as error:
                failed.append("%s: %s" % (name, error))
                continue
            for stage, glsl in (("vert", vertex), ("frag", fragment)):
                if glsl is None:
                    continue
                source = os.path.join(work, "%s.%s" % (name, stage))
                with open(source, "w", newline="\n") as f:
                    f.write(glsl)
                output = os.path.join(args.output, "%s.%s.dksh" % (name, stage))
                result = subprocess.run([uam, "-s", stage, "-o", output, source], capture_output=True, text=True)
                if result.returncode != 0:
                    failed.append("%s.%s: %s" % (name, stage, (result.stdout + result.stderr).strip()[:400]))
                else:
                    compiled += 1

    print("%d shader stages compiled to %s" % (compiled, args.output))
    for line in failed:
        print("FAILED " + line)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
