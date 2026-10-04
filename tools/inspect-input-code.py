"""Read-only inspection of runtime methods recorded by the Lua inspector.

Usage: python tools/inspect-input-code.py PID REPORT [method-name ...]
Requires the optional capstone Python package. No process writes or threads.
"""
import argparse
import re
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.ReadProcessMemory.restype = w.BOOL
k.CloseHandle.argtypes = [w.HANDLE]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pid', type=int)
parser.add_argument('report', type=Path)
parser.add_argument('methods', nargs='+')
parser.add_argument('--output', type=Path, default=root / 'build/runtime-inspection')
args = parser.parse_args()
handle = k.OpenProcess(0x0010, False, args.pid)
if not handle:
    raise c.WinError(c.get_last_error())
try:
    reports = [args.report]
    extra = args.report.with_name('mhr_gyro_action_input.json')
    if extra.exists() and extra not in reports:
        reports.append(extra)
    report = {'types': [t for path in reports for t in json.loads(path.read_text(encoding='utf-8-sig'))['types']]}
    methods = {(t['name'], m['name']): m for t in report['types'] for m in (t.get('methods') or [])}
    symbols = {int(m['address'].split()[-1], 16): f'{t}.{n}' for (t, n), m in methods.items() if m.get('address', '').startswith('userdata:')}
    selected = set(args.methods)
    args.output.mkdir(parents=True, exist_ok=True)
    for (type_name, name), method in methods.items():
        if name not in selected and f'{type_name}.{name}' not in selected:
            continue
        if not method.get('address', '').startswith('userdata:'):
            continue
        address = int(method['address'].split()[-1], 16)
        data, read = c.create_string_buffer(8192), c.c_size_t()
        if not k.ReadProcessMemory(handle, address, data, len(data), c.byref(read)):
            raise c.WinError(c.get_last_error())
        lines = [f'{type_name}.{name} @ {address:x}']
        for instruction in Cs(CS_ARCH_X86, CS_MODE_64).disasm(data.raw[:read.value], address):
            annotation = ''
            if instruction.mnemonic in ('call', 'jmp') and instruction.op_str.startswith('0x'):
                annotation = symbols.get(int(instruction.op_str, 16), '')
            lines.append(f'{instruction.address:x} {instruction.mnemonic:8} {instruction.op_str:46} {annotation}')
            if instruction.mnemonic == 'int3':
                break
        filename = re.sub(r'[^a-zA-Z0-9_.-]', '_', f'{type_name}.{name}')
        output = args.output / (filename + '.asm.txt')
        output.write_text('\n'.join(lines))
        print(output, len(lines), 'lines')
finally:
    k.CloseHandle(handle)
