"""Case-insensitive host preparation for the pinned SimpleInit snapshot."""
import hashlib
import json
from pathlib import Path
import re

WRAPPER_SHA256 = 'fdfd0d23b6157f6ebfe1b3a2db1aca00be58838d9e2325554e490dc975da5d15'
INCLUDE = re.compile(r'(^\s*#\s*include\s*[<"])uefi\.h([>"])', re.MULTILINE)


def prepare_simple_init(directory):
    directory = Path(directory).resolve()
    original = directory / 'include/uefi.h'
    renamed = directory / 'include/simpleinit_uefi.h'
    marker = directory / 'macos-case-preparation.json'
    if marker.exists():
        record = json.loads(marker.read_text())
        for name, digest in record['files'].items():
            if hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
                raise ValueError('SimpleInit case preparation changed: ' + name)
        return record
    if renamed.exists() or hashlib.sha256(original.read_bytes()).hexdigest() != WRAPPER_SHA256:
        raise ValueError('Unknown SimpleInit UEFI wrapper; refuse implicit source drift')
    changes = {}
    for path in directory.rglob('*'):
        if path.suffix not in ('.h', '.c', '.cpp', '.S') or not path.is_file():
            continue
        raw = path.read_bytes()
        text = raw.decode('utf-8')
        changed, count = INCLUDE.subn(r'\1simpleinit_uefi.h\2', text)
        if count:
            changes[path] = changed.encode('utf-8')
    if not changes:
        raise ValueError('Expected lowercase wrapper references')
    original.rename(renamed)
    for path, data in changes.items():
        path.write_bytes(data)
    files = [renamed, *changes]
    record = {'scope': 'Header naming only; EDK2 Uefi.h remains distinct',
              'original_wrapper_sha256': WRAPPER_SHA256,
              'files': {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in files}}
    marker.write_text(json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    import sys
    record = prepare_simple_init(sys.argv[1])
    print('SimpleInit case-safe header preparation:', len(record['files']), 'files')
