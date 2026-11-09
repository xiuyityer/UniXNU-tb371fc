#!/usr/bin/env bash
set -euo pipefail
ROOT="${UNIXNU_RESUME_ROOT:?set by build_v135.py}"
BOARD="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${UNIXNU_FIRMWARE_SOURCE:-$BOARD/build/menu-firmware/source}"
SIMPLE="${UNIXNU_SIMPLE_INIT:-$SRC/GPLDrivers/Library/SimpleInit}"
OUT="${UNIXNU_FIRMWARE_OUTPUT:-$BOARD/build/menu-firmware/output}"
LLVM="$ROOT/tools/llvm-local"
if [[ "$(uname -s)" != Darwin ]]; then echo "macOS required" >&2; exit 1; fi
if [[ "$(uname -s)" == Darwin ]]; then
  LLVM_BIN="${UNIXNU_NATIVE_TOOLS:?native LLVM tool directory required}"
  JOBS="${UNIXNU_JOBS:-$(sysctl -n hw.logicalcpu)}"
  export PATH="$LLVM_BIN:$PATH"
  export CLANG38_BIN="$LLVM_BIN/"
  # Firmware is AArch64 ELF, linked by LLVM lld, then converted by GenFw.
  # Apple's Mach-O ld and an absent GNU cross-prefix must never be selected.
  unset CROSS_COMPILE GCC5_AARCH64_PREFIX CLANG38_AARCH64_PREFIX
else
  LLVM_BIN="$LLVM/usr/bin"
  JOBS="${UNIXNU_JOBS:-$(nproc)}"
  export PATH="$LLVM_BIN:$PATH"
  export LD_LIBRARY_PATH="$LLVM/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export CLANG38_BIN="$LLVM_BIN/"
  export CROSS_COMPILE="$LLVM_BIN/aarch64-linux-gnu-"
  export GCC5_AARCH64_PREFIX="$CROSS_COMPILE"
  export CLANG38_AARCH64_PREFIX="$CROSS_COMPILE"
fi
export EDK2="$SRC/Common/edk2"
export EDK2_PLATFORMS="$SRC/Common/edk2-platforms"
export SIMPLE_INIT="$SIMPLE"
export PACKAGES_PATH="$EDK2:$EDK2_PLATFORMS:$SIMPLE:$SRC:$SRC/GPLDrivers"
export WORKSPACE="$OUT/workspace"
mkdir -p "$OUT" "$WORKSPACE" "$SIMPLE/build"
if [[ "$(uname -s)" == Darwin ]]; then
  python3 "$BOARD/scripts/macos_firmware_tools.py" "$SIMPLE"
fi
# Restore the original FDT-only preparation after reuse of a retained source tree.
python3 - "$SIMPLE" <<'PY_SIMPLE'
from pathlib import Path
import sys
p=Path(sys.argv[1])
f=p/'SimpleInit.inc';f.write_text(f.read_text().split('[Components]')[0])
f=p/'SimpleInit.dec';f.write_text(f.read_text().replace('  libs/freetype/include\n',''))
(p/'src/config-uefi.h').write_text('#define ENABLE_UEFI 1\n#define ENABLE_FDT 1\n')
PY_SIMPLE

# Compile translated strings only; do not generate or stage a Linux rootfs.
for po in "$SIMPLE"/po/*.po; do
  [[ -f "$po" ]] || continue
  lang="$(basename "$po" .po)"
  target="$SIMPLE/root/usr/share/locale/$lang/LC_MESSAGES"
  mkdir -p "$target"
  msgfmt -o "$target/simple-init.mo" "$po"
done
cd "$EDK2"
set +u
source edksetup.sh
set -u
if [[ ! -x BaseTools/Source/C/bin/GenFw ]]; then
  if [[ "$(uname -s)" == Darwin ]]; then
    make -C BaseTools -j"$JOBS" BUILD_CC=/usr/bin/clang BUILD_CXX=/usr/bin/clang++ BUILD_AR=/usr/bin/ar
  else
    C_INCLUDE_PATH="$LLVM/usr/include" CPLUS_INCLUDE_PATH="$LLVM/usr/include" LIBRARY_PATH="$LLVM/usr/lib/x86_64-linux-gnu" make -C BaseTools -j"$JOBS"
  fi
fi
cd "$SRC"
mkdir -p "$WORKSPACE/Conf"
cp "$EDK2/BaseTools/Conf/target.template" "$WORKSPACE/Conf/target.txt"
export CONF_PATH="$WORKSPACE/Conf"
cp "$SRC/tools/tools_def.txt" "$WORKSPACE/Conf/tools_def.txt"
if [[ "$(uname -s)" == Darwin ]]; then
  python3 - "$WORKSPACE/Conf/tools_def.txt" "$LLVM_BIN" <<'PY_MAC_TOOLS'
from pathlib import Path
import sys
p=Path(sys.argv[1]); prefix=Path(sys.argv[2]); text=p.read_text(); rows=[]
for row in text.splitlines():
    if 'CLANG38_AARCH64_RC_PATH' in row:
        row=row.split('=')[0]+'= ENV(CLANG38_BIN)llvm-objcopy'
    if 'CLANG38_AARCH64_' in row and 'DLINK' in row and 'FLAGS' in row:
        row=row.replace('-fuse-ld=lld', '--ld-path='+str(prefix/'ld.lld'))
    rows.append(row)
result='\n'.join(rows)+'\n'
if '--ld-path='+str(prefix/'ld.lld') not in result:
    raise SystemExit('Expected reviewed ELF linker flags; refuse unknown tools definition')
p.write_text(result)
PY_MAC_TOOLS
fi
cp "$SRC/tools/build_rule.txt" "$WORKSPACE/Conf/build_rule.txt"
# Repository's own deterministic ACPI AML is checked in; no Wine/IASL regeneration.
source configs/devices/tb371fc.conf
source configs/sm8250.conf
source configs/devices/tb371fc.conf
build -n "$JOBS" -a AARCH64 -t CLANG38 -p Platform/Lenovo/sm8250/tb371fc.dsc \
  -b RELEASE -D FIRMWARE_VER=tb371fc-menu-hostbuild -D USE_UART=1 -D FIX_CLANG=0 \
  -D NO_EXCEPTION_DISPLAY=0 -D FD_BASE=0xCE000000 -D FD_SIZE="${UNIXNU_FD_SIZE:-0x00700000}" \
  -D ENABLE_LINUX_UTILS=0 -D SECURE_BOOT_ENABLE=FALSE -D AB_SLOTS_SUPPORT=FALSE \
  -D ENABLE_SIMPLE_INIT=0 -D ENABLE_LINUX_SIMPLE_MASS_STORAGE=0
mkdir -p "$OUT"
cp "$WORKSPACE/Build/tb371fc/RELEASE_CLANG38/FV/SM8250_UEFI.fd" "$OUT/tb371fc-menu.fd"
python3 - "$OUT/tb371fc-menu.fd" <<'PY_DIGEST'
import hashlib,sys
from pathlib import Path
p=Path(sys.argv[1]); digest=hashlib.sha256(p.read_bytes()).hexdigest()
line=digest+'  '+str(p)+'\n';p.with_name('firmware.sha256').write_text(line)
print(line,end='');print('bytes='+str(p.stat().st_size))
PY_DIGEST
