#!/usr/bin/env python3
"""Run the DOM fixture when Node.js is available, with an explicit CTest skip otherwise."""

import pathlib
import re
import shutil
import subprocess
import sys


def main():
    executable = shutil.which("node") or shutil.which("nodejs")
    if executable is None:
        print("SKIP: Node.js 18+ is required to run the upload DOM fixture")
        return 77
    version = subprocess.run([executable, "--version"], capture_output=True, text=True, check=False)
    match = re.fullmatch(r"v?(\d+)\.\d+\.\d+", version.stdout.strip())
    if version.returncode != 0 or match is None or int(match.group(1)) < 18:
        print("SKIP: Node.js 18+ is required to run the upload DOM fixture")
        return 77
    fixture = pathlib.Path(__file__).with_name("test_upload_dom.cjs")
    return subprocess.run([executable, str(fixture)], check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
