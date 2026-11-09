"""Exact V129-derived timing query fix, preserving every other instruction."""
import hashlib
import json
from pathlib import Path
import struct

PARENT_SHA = 'a33b49a4b349d32ca405ab1e5847cfc7c21fdc3e75c8839e4d587ad51cac6f22'
QUERY_VA = 0xfffffff007a08e2c
BEFORE = bytes.fromhex('fd7bbfa9fd030091e2031eaae10300aa202b80520f070094')
# bool backendTimingEnabled() { return false; }, followed by unreachable NOPs.
AFTER = bytes.fromhex('00008052c0035fd61f2003d51f2003d51f2003d51f2003d5')

def sha(b): return hashlib.sha256(b).hexdigest()

def query_offset(b):
    at = 32
    for _ in range(struct.unpack_from('<I', b, 16)[0]):
        cmd, size = struct.unpack_from('<II', b, at)
        if cmd == 0x19:
            va, _, off, count = struct.unpack_from('<4Q', b, at + 24)
            if va <= QUERY_VA and QUERY_VA + len(BEFORE) <= va + count:
                return off + QUERY_VA - va
        at += size
    raise ValueError('Timing query VA is outside the retained kernel')

def patched(parent):
    assert sha(parent) == PARENT_SHA
    off = query_offset(parent)
    assert parent[off:off + 24] == BEFORE and len(AFTER) == len(BEFORE)
    assert parent[1179394:1179405] == b'%s.tb-v131\0'
    out = bytearray(parent)
    out[off:off + 24] = AFTER
    out[1179394:1179405] = b'%s.tb-v135\0'
    return bytes(out)
