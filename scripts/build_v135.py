#!/usr/bin/env python3
"""Rebuild V135 firmware and Android boot using the owner's private resume inputs.

XNU is the exact retained binary derivative, not a complete source rebuild.
No device commands. Python 3.11+, macOS, native LLVM/mtools/gettext required.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import tomllib
import zlib
from display_timeout_v135 import patched, sha

ROOT=Path(__file__).resolve().parents[1]
KERNEL_SHA='342a0be94069c26d0d46a1e1fac03e4a396d89ea09bbc5c5acc5b5336ee10f23'
FD_SHA='1bb5ca3c2535fa9b35513b74258045ff86116b56c6f338fff86d309a6b5c9435'

def run(args,env,log):
    subprocess.run([str(v) for v in args],env=env,check=True,stdout=log,stderr=subprocess.STDOUT)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--inputs',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--tools',type=Path,help='Directory containing native LLVM, mtools and gettext (otherwise PATH)')
    ap.add_argument('--quick-pack',action='store_true',help='Compile BootShim and package retained V135 FD; skip firmware rebuild')
    a=ap.parse_args(); start=time.monotonic()
    if sys.platform!='darwin': raise SystemExit('Use native macOS')
    src=a.inputs.resolve(); out=a.output.resolve()
    manifest=json.loads((src/'inputs.json').read_text())
    if manifest['baseline']!='V135': raise ValueError('Expected V135 inputs')
    for name,item in manifest['files'].items():
        path=(src/name).resolve(); path.relative_to(src)
        if path.stat().st_size!=item['bytes'] or sha(path.read_bytes())!=item['sha256']:
            raise ValueError('Private input differs: '+name)
    env=dict(os.environ)
    if a.tools: env['PATH']=str(a.tools.resolve())+os.pathsep+env['PATH']
    needed=['clang','llvm-objcopy']+([] if a.quick_pack else ['ld.lld','llvm-ar','mformat','mcopy','msgfmt'])
    found={n:shutil.which(n,path=env['PATH']) for n in needed}
    if not all(found.values()): raise ValueError('Missing native tools: '+str([k for k,v in found.items() if not v]))
    tool_manifest=json.loads((src/'native-tools/TOOLS.json').read_text())
    for item in tool_manifest['files']:
        tool=(src/item['path']).resolve()
        tool.relative_to(src)
        if tool.is_symlink() or tool.stat().st_size!=item['bytes'] or sha(tool.read_bytes())!=item['sha256']:
            raise ValueError('Private native tool changed: '+item['path'])
    for tool in found.values():
        desc=subprocess.check_output(['/usr/bin/file','-L',tool],text=True)
        if 'Mach-O' not in desc: raise ValueError('Tool is not native macOS: '+desc)
    out.mkdir(parents=True,exist_ok=False)
    kernel=patched((src/'kernel-parent.macho').read_bytes())
    if sha(kernel)!=KERNEL_SHA: raise ValueError('V135 kernel mismatch')
    (out/'kernel.macho').write_bytes(kernel)
    with (out/'build.log').open('w') as log:
        if a.quick_pack:
            fd=(src/'tb371fc-menu.fd').read_bytes()
            if sha(fd)!=FD_SHA: raise ValueError('Retained V135 firmware differs')
        else:
            stage=out/'firmware-source'
            shutil.copytree(src/'firmware-source',stage,symlinks=True)
            tree=out/'payload-tree';shutil.copytree(src/'payload-tree',tree)
            (tree/'Payloads/kernel.macho').write_bytes(kernel)
            cfg=tomllib.loads((tree/'EFI/UniXNU/unixnu.toml').read_text())
            entries=[e for e in cfg['entry'] if e['type']=='xnu']
            if cfg['boot']['default']!='probe' or cfg['boot']['timeout']!=-1 or cfg['boot']['fallback_timeout']!=-1 or len(entries)!=1 or 'V135' not in entries[0]['title'] or not entries[0].get('confirm'):
                raise ValueError('Manual V135 menu differs')
            for p in [tree,*tree.rglob('*')]: os.utime(p,(315532800,315532800))
            fat=stage/'Tb371fcEmbeddedFat/Payload.fat'
            # Capsule deliberately excludes the generated FAT; each build creates it.
            env['MTOOLS_SKIP_CHECK']='1'
            run(['mformat','-C','-F','-c','1','-T','262144','-v','TB371FCBOOT','-N','371fc001','-i',fat,'::'],env,log)
            for name in ['EFI','Payloads']:run(['mcopy','-i',fat,'-s','-Q','-m',tree/name,'::/'],env,log)
            native=Path(found['ld.lld']).parent
            env.update(UNIXNU_RESUME_ROOT=str(out),UNIXNU_NATIVE_TOOLS=str(native),
                UNIXNU_FIRMWARE_SOURCE=str(stage),UNIXNU_FIRMWARE_OUTPUT=str(out/'firmware-output'),UNIXNU_FD_SIZE='0x2000000')
            run(['/bin/bash',ROOT/'scripts/build_firmware_v135.sh'],env,log)
            fd=(out/'firmware-output/tb371fc-menu.fd').read_bytes()
            if len(fd)!=33554432: raise ValueError('Firmware span differs')
        # Compile the actual AArch64 ELF shim every time, including quick mode.
        run([found['clang'],'--target=aarch64-none-elf','-c',src/'firmware-source/tools/BootShim/BootShim.S','-o',out/'BootShim.elf','-DUEFI_BASE=0xCE000000','-DUEFI_SIZE=0x2000000'],env,log)
        run([found['llvm-objcopy'],'-O','binary',out/'BootShim.elf',out/'BootShim.bin'],env,log)
    shim=(out/'BootShim.bin').read_bytes()
    if struct.unpack_from('<QQ',shim,72)!=(0xce000000,len(fd)): raise ValueError('Shim span mismatch')
    dtb=(src/'firmware-source/Platform/Lenovo/sm8250/FdtBlob_compat/tb371fc.dtb').read_bytes()
    if dtb[:4]!=bytes.fromhex('d00dfeed') or struct.unpack_from('>I',dtb,4)[0]!=len(dtb): raise ValueError('DTB differs')
    packed=gzip.compress(shim+fd,mtime=0)+dtb
    target=out/'boot-tb371fc-embedded-payload.img'
    work=out/'packaging';work.mkdir(exist_ok=True)
    (work/'kernel').write_bytes(packed);(work/'ramdisk').write_bytes(b'')
    with (out/'build.log').open('a') as log:
        run([src/'firmware-source/tools/mkbootimg.py','--kernel',work/'kernel',
            '--ramdisk',work/'ramdisk','--base','0x10000000','--kernel_offset','0',
            '--ramdisk_offset','0','--tags_offset','0','--os_version','11.0.0',
            '--os_patch_level','2024-02','--header_version','1','--pagesize','2048',
            '-o',target],env,log)
    boot=target.read_bytes()
    if len(boot)>=0x6000000: raise ValueError('Boot image too large')
    ksize=struct.unpack_from('<I',boot,8)[0]
    decoder=zlib.decompressobj(31); raw=decoder.decompress(boot[2048:2048+ksize])+decoder.flush()
    if raw!=shim+fd or decoder.unused_data!=dtb:raise ValueError('Boot roundtrip failed')
    result=dict(baseline='V135',artifact=target.name,bytes=len(boot),sha256=sha(boot),kernel_sha256=sha(kernel),firmware_sha256=sha(fd),firmware_compiled=not a.quick_pack,bootshim_compiled=True,full_xnu_source_build=False,device_tested_this_build=False,elapsed_seconds=round(time.monotonic()-start,2))
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'SHA256SUMS').write_text(sha(boot)+'  '+target.name+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
