"""Read Windows logical processor topology and live parking flags."""
import ctypes as c
from ctypes import wintypes as w
import json
import struct
import time


api = c.WinDLL('kernel32', use_last_error=True).GetSystemCpuSetInformation
api.argtypes = (c.c_void_p, w.ULONG, c.POINTER(w.ULONG), w.HANDLE, w.ULONG)
api.restype = w.BOOL


def snapshot():
    length = w.ULONG()
    api(None, 0, c.byref(length), None, 0)
    if length.value == 0:
        raise c.WinError(c.get_last_error())
    data = c.create_string_buffer(length.value)
    if not api(data, len(data), c.byref(length), None, 0):
        raise c.WinError(c.get_last_error())
    offset = 0
    cpus = []
    while offset < length.value:
        size, kind = struct.unpack_from('<II', data, offset)
        if size < 8 or offset+size > length.value:
            raise ValueError('Invalid SYSTEM_CPU_SET_INFORMATION record')
        if kind == 0:
            if size < 20:
                raise ValueError('Incomplete CPU Set entry')
            _, group, lp, core, _, _, efficiency, flags = struct.unpack_from('<IH6B', data, offset+8)
            cpus.append(dict(group=group, logical_cpu=lp, core=core, efficiency_class=efficiency,
                             parked=bool(flags & 1), allocated=bool(flags & 2)))
        offset += size
    if not cpus or len({(x['group'],x['logical_cpu']) for x in cpus}) != len(cpus):
        raise ValueError('Incomplete or duplicate CPU topology')
    return sorted(cpus, key=lambda x:(x['group'],x['logical_cpu']))


def samples(count):
    observed = []
    for index in range(count):
        if index:
            time.sleep(.05)
        observed.append(snapshot())
    return observed


if __name__ == '__main__':
    print(json.dumps(samples(20), separators=(',',':')))
