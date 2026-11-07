"""Operate a single temporary clone of the original Windows power scheme."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

from cpu_sets import snapshot as cpu_sets


POWERCFG = Path(os.environ['WINDIR'])/'System32/powercfg.exe'
GUID = re.compile(r'[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}')
SETTING = re.compile(r'^ {4}\S.*?guid:\s*('+GUID.pattern+r')\b')
INDEX = re.compile(r'^ {4}\S.*?0x([0-9a-f]{8})\s*$')
PARKING = ('0cc5b647-c1df-4637-891a-dec35c318583', '0cc5b647-c1df-4637-891a-dec35c318584')
powrprof = c.WinDLL('powrprof', use_last_error=True)
get_scheme = powrprof.PowerGetActiveScheme
get_scheme.argtypes = (w.HANDLE, c.POINTER(c.c_void_p))
get_scheme.restype = w.DWORD
set_scheme = powrprof.PowerSetActiveScheme
set_scheme.argtypes = (w.HANDLE, c.c_void_p)
set_scheme.restype = w.DWORD
free = c.WinDLL('kernel32').LocalFree
free.argtypes = (w.HANDLE,)
free.restype = w.HANDLE


def cfg(*args):
    try:
        result = subprocess.run([str(POWERCFG), *args], capture_output=True,
                                timeout=4, creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f'powercfg {args[0]} timed out') from exc
    if result.returncode:
        raise RuntimeError(f'powercfg {args[0]} exited {result.returncode}')
    return result.stdout.decode('ascii', errors='replace').lower()


def active_scheme():
    address = c.c_void_p()
    error = get_scheme(None, c.byref(address))
    if error:
        raise c.WinError(error)
    try:
        return str(uuid.UUID(bytes_le=c.string_at(address, 16)))
    finally:
        free(address)


def activate(scheme):
    value = c.create_string_buffer(uuid.UUID(scheme).bytes_le, 16)
    error = set_scheme(None, value)
    if error:
        raise c.WinError(error)


def settings(scheme):
    result = {}
    key = None
    for line in cfg('/qh', scheme).splitlines():
        item = SETTING.match(line)
        value = INDEX.match(line)
        if item:
            key = item[1]
            if key in result:
                raise ValueError('Duplicate power setting')
            result[key] = []
        elif key and value:
            result[key].append(int(value[1], 16))
    if len(result) < 50 or any(len(values) != 2 for values in result.values()):
        raise ValueError('Incomplete AC/DC power setting readback')
    return result


class ParkingPolicy:
    def __init__(self, protocol):
        self.baseline = protocol['baseline_power_scheme']
        self.temporary = protocol['temporary_power_scheme']
        self.minimums = protocol['parking_minimums']
        self.preferences = protocol['processor_preferences']
        allowed = {'93b8b6dc-0698-4d1c-9ee4-0644e900c85d': 2, '36687f9e-e3a5-4dbf-b1dc-15eb381c6863': 0, '36687f9e-e3a5-4dbf-b1dc-15eb381c6864': 0}
        if self.preferences not in ({}, allowed):
            raise ValueError('Unexpected processor preference overrides')
        if not all(GUID.fullmatch(value) for value in (self.baseline, self.temporary)):
            raise ValueError('Invalid scheme identity')
        if self.baseline == self.temporary:
            raise ValueError('Temporary policy would overwrite baseline')

    def create(self):
        self.verify(False)
        if self.temporary in cfg('/list'):
            raise ValueError('Temporary scheme already exists')
        baseline = settings(self.baseline)
        cfg('/duplicatescheme', self.baseline, self.temporary)
        changes = dict(zip(PARKING,self.minimums), **self.preferences)
        for mode in ('/setacvalueindex', '/setdcvalueindex'):
            for setting,value in changes.items():
                cfg(mode,self.temporary,'SUB_PROCESSOR',setting,str(value))
        self.verify(False)
        candidate = settings(self.temporary)
        if not changes.keys() <= baseline.keys() or candidate != dict(baseline, **{key:[value,value] for key,value in changes.items()}):
            raise ValueError('Unexpected changes in the temporary scheme')
        if settings(self.baseline) != baseline:
            raise ValueError('Original power scheme changed')
        return dict(settings_checked=len(baseline),
                    baseline_parking={key:baseline[key] for key in PARKING},
                    temporary_parking={key:candidate[key] for key in PARKING},
                    baseline_preferences={key:baseline[key] for key in self.preferences},
                    temporary_preferences={key:candidate[key] for key in self.preferences},
                    baseline_settings=baseline, temporary_settings=candidate)

    def verify(self, unparked):
        current = active_scheme()
        if current != (self.temporary if unparked else self.baseline):
            raise ValueError('Windows power scheme changed during the pilot')
        return current

    def enter(self):
        self.verify(False)
        activate(self.temporary)

    def restore(self):
        if active_scheme() == self.temporary:
            activate(self.baseline)
        self.verify(False)

    def cleanup(self):
        self.restore()
        if self.temporary in cfg('/list'):
            cfg('/delete', self.temporary)
        if self.temporary in cfg('/list'):
            raise ValueError('Could not delete the temporary scheme')

    @staticmethod
    def parked():
        values = cpu_sets()
        if len(values) != 12 or any(row['group'] != 0 for row in values):
            raise ValueError('Host CPU Set topology changed')
        return [row['logical_cpu'] for row in values if row['parked']]

    def ready(self, unparked):
        time.sleep(1)
        self.verify(unparked)
        if unparked and self.parked():
            raise ValueError('Some logical processors remain parked')
