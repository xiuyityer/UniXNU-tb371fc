#!/usr/bin/env python3
"""Validate the public source snapshot; no network or device operations."""
import hashlib
import json
from pathlib import Path
import re
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_DIRS = {'build', 'dist', 'out', 'research', 'tools', '.cache', 'vendor', '__pycache__'}
PRIVATE_SUFFIXES = {'.img', '.bin', '.fw', '.dmg', '.ipsw', '.o', '.a', '.macho', '.pem', '.key', '.pyc'}

def check():
    manifest = json.loads((ROOT/'upstream/source-manifest.json').read_text())
    errors = []
    paths = set()
    for item in manifest['files']:
        rel = Path(item['path'])
        if rel.is_absolute() or '..' in rel.parts or item['path'] in paths:
            errors.append('Invalid/duplicate source path: '+item['path']); continue
        paths.add(item['path'])
        path = ROOT/rel
        if not path.is_file() or path.is_symlink():
            errors.append('Source absent or symlink: '+item['path']); continue
        raw = path.read_bytes()
        if len(raw) != item['bytes'] or hashlib.sha256(raw).hexdigest() != item['sha256']:
            errors.append('Source digest differs: '+item['path'])
    if (ROOT/'.git').exists():
        listing = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '--cached', '--others', '--exclude-standard', '-z'])
        candidates = [ROOT/name.decode() for name in listing.split(b'\0') if name]
    else:
        candidates = [p for p in ROOT.rglob('*') if p.is_file() or p.is_symlink()]
    for path in candidates:
        rel = path.relative_to(ROOT)
        if '.git' in rel.parts: continue
        if rel.parts[0] in PRIVATE_DIRS or '__pycache__' in rel.parts:
            errors.append('Private/generated directory: '+str(rel)); continue
        if path.is_symlink():
            errors.append('Symlink in public snapshot: '+str(rel)); continue
        if not path.is_file(): continue
        if path.suffix.lower() in PRIVATE_SUFFIXES or path.name.startswith('.env'):
            errors.append('Private/generated file: '+str(rel))
        raw = path.read_bytes()
        if len(raw) > 2*1024*1024 or b'\0' in raw:
            errors.append('Unexpected binary or large payload: '+str(rel))
        if path.suffix in ('.c','.cpp','.h','.mm','.rs','.patch','.inf','.dec','.dsc') and str(rel) not in paths and path.parts[-2] != 'patches':
            errors.append('Source missing from manifest: '+str(rel))
    lock = json.loads((ROOT/'upstream/sources.lock.json').read_text())
    for source in lock['sources']:
        if not re.fullmatch('[0-9a-f]{40}', source['commit']):
            errors.append('Unpinned upstream: '+source['name'])
        if not source['repository'].startswith('https://'):
            errors.append('Unexpected upstream URL: '+source['name'])
    lifecycle=(ROOT/'package/Tb371fcDisplay/DisplayLifecycle.cpp').read_text()
    if lifecycle.count('    tb371fc_input_start();') != 1:
        errors.append('V128 input startup call differs')
    alloc=(ROOT/'drivers/Adreno650/NativeAdrenoUserClient.cpp').read_text()
    if 'prefer_contiguous' in alloc or 'request->contiguous!=0,(size_t)request->bytes,true);' not in alloc:
        errors.append('V129 allocation policy differs')
    if errors:
        for line in errors: print('ERROR:',line,file=sys.stderr)
        return 1
    print('PASS:',len(paths),'source/license files match; upstream commits pinned; no generated/private payload files')
    print('This checks repository content only, not complete builds or hardware behavior.')
    return 0

if __name__ == '__main__':
    sys.exit(check())
