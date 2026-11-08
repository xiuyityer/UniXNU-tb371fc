#!/usr/bin/env python3
"""Run the pinned official catalog validator. Python 3.11+, network; no device I/O."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
REVISION = "5dc00d201496a1e7a72d1f5db23f041eb94ace1c"
FILES = {'scripts/porting-record.py': '6621f5645a2fc1019ad286decc5f2b9d5918313f94d600b4caff4024b702062a', 'components.toml': 'af721e73ce1923675675e98579514e3f0f1593a4c0192e17bb364d701a0131a8'}


def main():
    if sys.version_info < (3, 11):
        raise SystemExit("Catalog validation requires Python 3.11 or newer")
    with tempfile.TemporaryDirectory(prefix="unixnu-catalog-") as folder:
        base = Path(folder)
        for name, expected in FILES.items():
            url = f"https://raw.githubusercontent.com/UniXNU/UniXNU/{REVISION}/{name}"
            with urlopen(url, timeout=30) as response:
                raw = response.read(2 * 1024 * 1024 + 1)
            if hashlib.sha256(raw).hexdigest() != expected:
                raise SystemExit(f"Official input digest differs: {name}")
            target = base / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
        subprocess.run([sys.executable, str(base / "scripts/porting-record.py"),
                        "check", "--device", str(ROOT)], check=True)
    print("PASS: pinned official catalog schema and evidence paths; no system build or hardware test")


if __name__ == "__main__":
    main()
