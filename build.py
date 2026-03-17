#!/usr/bin/env python3
import os
import sys
import subprocess
from pathlib import Path

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <pxr_install_dir>", file=sys.stderr)
        sys.exit(1)

    pxr_dir = sys.argv[1]
    conda_prefix = os.environ.get("CONDA_PREFIX", "")
    build_dir = Path("build")
    build_dir.mkdir(exist_ok=True)

    subprocess.run([
        "cmake", "..",
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DCMAKE_PREFIX_PATH={conda_prefix};{pxr_dir}",
        f"-DCPLUS_INCLUDE_PATH={conda_prefix}/include/",
    ], cwd=build_dir, check=True)

    subprocess.run(["cmake", "--build", "."], cwd=build_dir, check=True)

if __name__ == "__main__":
    main()
