#!/usr/bin/env python3
"""Archive/restore a local sparse image, preserving holes; never opens a device."""
import argparse
import errno
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat

CHUNK = 8 * 1024 * 1024

def regular(path):
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('Expected a regular local file')

def pack(source, output):
    regular(source)
    output.mkdir(parents=True, exist_ok=False)
    size = source.stat().st_size
    rows = []
    with source.open('rb') as f:
        end = 0
        while end < size:
            try:
                start = os.lseek(f.fileno(), end, os.SEEK_DATA)
            except OSError as e:
                if e.errno == errno.ENXIO: break
                raise
            end = min(os.lseek(f.fileno(), start, os.SEEK_HOLE), size)
            if end <= start: raise ValueError('Invalid sparse extent')
            f.seek(start)
            while f.tell() < end:
                offset = f.tell()
                raw = f.read(min(CHUNK, end-offset))
                if not raw: raise ValueError('Short source read')
                name = f'{len(rows):06d}.gz'
                packed = gzip.compress(raw, compresslevel=1, mtime=0)
                (output/name).write_bytes(packed)
                rows.append(dict(file=name, offset=offset, bytes=len(raw),
                    sha256=hashlib.sha256(raw).hexdigest(),
                    compressed_sha256=hashlib.sha256(packed).hexdigest()))
    (output/'sparse.json').write_text(json.dumps(dict(schema_version=1,
        logical_bytes=size, extent_bytes=sum(x['bytes'] for x in rows), chunks=rows), indent=2)+'\n')
    print(json.dumps(dict(logical_bytes=size, chunks=len(rows), output=str(output))))

def unpack(source, output):
    m=json.loads((source/'sparse.json').read_text())
    if m['schema_version'] != 1 or m['logical_bytes'] < 0: raise ValueError('Invalid sparse archive')
    previous=0
    for row in m['chunks']:
        name=row['file']
        if Path(name).name != name or row['offset'] < previous or not 0 < row['bytes'] <= CHUNK:
            raise ValueError('Invalid sparse chunk')
        previous=row['offset']+row['bytes']
        if previous > m['logical_bytes']: raise ValueError('Extent outside image')
    with output.open('xb') as f:
        f.truncate(m['logical_bytes'])
        for row in m['chunks']:
            p=source/row['file']; regular(p)
            raw=p.read_bytes()
            if hashlib.sha256(raw).hexdigest()!=row['compressed_sha256']: raise ValueError('Archive digest differs')
            with gzip.GzipFile(fileobj=__import__('io').BytesIO(raw)) as stream:
                data=stream.read(row['bytes']+1)
            if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
                raise ValueError('Image extent digest differs')
            f.seek(row['offset']); f.write(data)
        f.flush(); os.fsync(f.fileno())
    print(json.dumps(dict(restored=str(output), logical_bytes=m['logical_bytes'], chunks=len(m['chunks']))))

def verify(source):
    import io
    m=json.loads((source/'sparse.json').read_text())
    extent_bytes=0
    for row in m['chunks']:
        name=row['file']
        if Path(name).name!=name: raise ValueError('Invalid sparse chunk name')
        p=source/name;regular(p);packed=p.read_bytes()
        if hashlib.sha256(packed).hexdigest()!=row['compressed_sha256']: raise ValueError('Archive digest differs')
        with gzip.GzipFile(fileobj=io.BytesIO(packed)) as stream: data=stream.read(row['bytes']+1)
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('Image extent digest differs')
        extent_bytes+=len(data)
    if extent_bytes!=m['extent_bytes']: raise ValueError('Extent count differs')
    print(json.dumps(dict(verified=str(source),logical_bytes=m['logical_bytes'],extent_bytes=extent_bytes,chunks=len(m['chunks']))))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['pack','unpack','verify']); ap.add_argument('source',type=Path); ap.add_argument('output',type=Path,nargs='?')
    a=ap.parse_args()
    if a.action=='pack':
        if a.output is None: ap.error('pack requires output')
        pack(a.source,a.output)
    elif a.action=='unpack':
        if a.output is None: ap.error('unpack requires output')
        unpack(a.source,a.output)
    else: verify(a.source)
