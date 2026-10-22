#!/usr/bin/env python3
"""Run a CI native check under the Windows debugger API and print crash stacks."""
import ctypes as C
import os
import struct
import subprocess
import sys


def main():
    if os.name != 'nt':
        return subprocess.call(sys.argv[1:])
    kernel = C.WinDLL('kernel32', use_last_error=True)
    debug = C.WinDLL('dbghelp', use_last_error=True)
    pointer, uint = C.c_void_p, C.c_uint32

    class Startup(C.Structure):
        _fields_ = [('cb', uint), ('reserved', pointer), ('desktop', pointer), ('title', pointer),
                    ('x', uint), ('y', uint), ('width', uint), ('height', uint),
                    ('columns', uint), ('rows', uint), ('fill', uint), ('flags', uint),
                    ('show', C.c_uint16), ('reserved_size', C.c_uint16), ('reserved_data', pointer),
                    ('stdin', pointer), ('stdout', pointer), ('stderr', pointer)]

    class Process(C.Structure):
        _fields_ = [('process', pointer), ('thread', pointer), ('pid', uint), ('tid', uint)]

    class Payload(C.Union):
        _fields_ = [('alignment', pointer), ('bytes', C.c_ubyte * 160)]

    class Event(C.Structure):
        _fields_ = [('code', uint), ('pid', uint), ('tid', uint), ('payload', Payload)]

    kernel.CreateProcessW.argtypes = [C.c_wchar_p, C.c_wchar_p, pointer, pointer, C.c_int,
                                     uint, pointer, C.c_wchar_p, C.POINTER(Startup), C.POINTER(Process)]
    kernel.WaitForDebugEvent.argtypes = [C.POINTER(Event), uint]
    kernel.ContinueDebugEvent.argtypes = [uint, uint, uint]
    kernel.GetStdHandle.argtypes, kernel.GetStdHandle.restype = [C.c_int], pointer
    kernel.CloseHandle.argtypes = [pointer]
    kernel.ReadProcessMemory.argtypes = [pointer, pointer, pointer, C.c_size_t, pointer]
    kernel.OpenThread.argtypes, kernel.OpenThread.restype = [uint, C.c_int, uint], pointer
    kernel.GetThreadContext.argtypes = [pointer, pointer]
    kernel.GetFinalPathNameByHandleW.argtypes = [pointer, C.c_wchar_p, uint, uint]
    debug.SymInitialize.argtypes = [pointer, C.c_char_p, C.c_int]
    debug.SymFromAddr.argtypes = [pointer, C.c_uint64, pointer, pointer]
    debug.StackWalk64.argtypes = [uint, pointer, pointer, pointer, pointer,
                                 pointer, pointer, pointer, pointer]
    startup, process = Startup(), Process()
    startup.cb, startup.flags = C.sizeof(Startup), 0x100
    startup.stdin, startup.stdout, startup.stderr = (kernel.GetStdHandle(n) for n in (-10, -11, -12))
    command = C.create_unicode_buffer(subprocess.list2cmdline(sys.argv[1:]))
    if not kernel.CreateProcessW(None, command, None, None, True, 2, None, None,
                                 C.byref(startup), C.byref(process)):
        raise C.WinError(C.get_last_error())
    modules = []

    def read(address, size):
        data = C.create_string_buffer(size)
        count = C.c_size_t()
        if not kernel.ReadProcessMemory(process.process, address, data, size, C.byref(count)):
            return b''
        return data.raw[:count.value]

    def module(file, base):
        path = C.create_unicode_buffer(32768)
        kernel.GetFinalPathNameByHandleW(file, path, len(path), 0)
        name = path.value or f'image@{base:x}'
        header = read(base, 64)
        size = 0
        if len(header) == 64:
            pe = struct.unpack_from('<I', header, 60)[0]
            image = read(base + pe + 80, 4)
            if len(image) == 4:
                size = struct.unpack('<I', image)[0]
        modules.append((base, base + size, name))
        print(f'[loader] {base:016x} {name}', flush=True)
        if file:
            kernel.CloseHandle(file)

    def symbol(address):
        data = C.create_string_buffer(88 + 1024)
        struct.pack_into('<I', data, 0, 88)
        struct.pack_into('<I', data, 80, 1024)
        displacement = C.c_uint64()
        name = ''
        if debug.SymFromAddr(process.process, address, C.byref(displacement), data):
            length = struct.unpack_from('<I', data, 76)[0]
            name = data.raw[84:84 + length].decode(errors='replace')
        for start, end, path in modules:
            if start <= address < end:
                return f'{path}+0x{address-start:x} {name}'
        return f'0x{address:x} {name}'

    def stack(tid, address):
        print('[crash] ' + symbol(address), flush=True)
        debug.SymInitialize(process.process, None, True)
        thread = kernel.OpenThread(0x48, False, tid)
        if not thread:
            return
        raw = C.create_string_buffer(1232 + 16)
        context = (C.addressof(raw) + 15) & ~15
        C.c_uint32.from_address(context + 48).value = 0x10000b
        if kernel.GetThreadContext(thread, context):
            frame = C.create_string_buffer(1024)
            for offset, register in ((0, 248), (32, 160), (48, 152)):
                struct.pack_into('<Q', frame, offset, C.c_uint64.from_address(context + register).value)
                struct.pack_into('<I', frame, offset + 12, 3)
            for _ in range(32):
                address = struct.unpack_from('<Q', frame)[0]
                if not address:
                    break
                print('[stack] ' + symbol(address), flush=True)
                if not debug.StackWalk64(0x8664, process.process, thread, frame, context, None,
                        C.cast(debug.SymFunctionTableAccess64, pointer),
                        C.cast(debug.SymGetModuleBase64, pointer), None):
                    break
        kernel.CloseHandle(thread)

    event = Event()
    while kernel.WaitForDebugEvent(C.byref(event), 120000):
        data = bytes(event.payload.bytes)
        continuation = 0x10002
        if event.code == 3:
            module(struct.unpack_from('<Q', data, 0)[0], struct.unpack_from('<Q', data, 24)[0])
        elif event.code == 6:
            module(struct.unpack_from('<Q', data, 0)[0], struct.unpack_from('<Q', data, 8)[0])
        elif event.code == 1:
            code = struct.unpack_from('<I', data)[0]
            first = struct.unpack_from('<I', data, 152)[0]
            if code != 0x80000003:
                continuation = 0x80010001
                if not first:
                    print(f'[exception] 0x{code:08x}', flush=True)
                    stack(event.tid, struct.unpack_from('<Q', data, 16)[0])
        elif event.code == 5:
            code = struct.unpack_from('<I', data)[0]
            kernel.ContinueDebugEvent(event.pid, event.tid, continuation)
            kernel.CloseHandle(process.thread)
            kernel.CloseHandle(process.process)
            return 0 if code == 0 else 1
        kernel.ContinueDebugEvent(event.pid, event.tid, continuation)
    raise C.WinError(C.get_last_error())


if __name__ == '__main__':
    sys.exit(main())
