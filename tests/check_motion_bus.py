"""Run the linked software-I2C/LIS2DH12 driver against a GPIO-level slave model.

Checks protocol and bounded fault paths. Does not validate electrical timing.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


class Lis2dh12Bus:
    def __init__(self):
        self.scl = self.sda = 1
        self.slave_low = False
        self.present = True
        self.stuck_clock = self.stuck_data = False
        self.regs = bytearray(128)
        self.regs[0x0f] = 0x33
        self.pointer = 0
        self.increment = False
        self.mode = 'idle'
        self.starts = self.stops = self.read_bytes = 0
        self.writes = []

    def data_line(self):
        return self.sda and not self.slave_low and not self.stuck_data

    def begin(self):
        self.mode = 'recv'
        self.bits = self.byte = 0
        self.phase = 'address'
        self.slave_low = False
        self.starts += 1

    def decoded(self):
        self.ack = self.present
        self.after_ack = 'recv'
        if self.phase == 'address':
            self.ack = self.ack and self.byte >> 1 == 0x19
            if self.byte & 1:
                self.after_ack = 'send'
            else:
                self.phase = 'pointer'
        elif self.phase == 'pointer':
            self.pointer = self.byte & 0x7f
            self.increment = bool(self.byte & 0x80)
            self.phase = 'data'
        else:
            self.regs[self.pointer] = self.byte
            self.writes.append((self.pointer, self.byte))
            if self.increment:
                self.pointer = (self.pointer+1) & 0x7f

    def edge(self, rising):
        if self.mode == 'recv':
            if rising:
                if self.bits < 8:
                    self.byte = (self.byte << 1) | int(self.sda)
                    self.bits += 1
                    if self.bits == 8:
                        self.decoded()
                else:
                    self.bits = 9
            elif self.bits == 8:
                self.slave_low = self.ack
            elif self.bits == 9:
                self.slave_low = False
                self.mode = self.after_ack if self.ack else 'idle'
                self.bits = self.byte = 0
                if self.mode == 'send':
                    self.tx = self.regs[self.pointer]
                    self.slave_low = not bool(self.tx & 0x80)
        elif self.mode == 'send':
            if rising:
                self.bits += 1
                if self.bits == 9:
                    self.master_ack = not self.sda
            elif self.bits < 8:
                self.slave_low = not bool(self.tx & (0x80 >> self.bits))
            elif self.bits == 8:
                self.slave_low = False
            else:
                self.read_bytes += 1
                if self.increment:
                    self.pointer = (self.pointer+1) & 0x7f
                self.bits = 0
                if self.master_ack:
                    self.tx = self.regs[self.pointer]
                    self.slave_low = not bool(self.tx & 0x80)
                else:
                    self.mode = 'idle'
                    self.slave_low = False

    def bsrr(self, value):
        old_clock, old_data = self.scl, self.data_line()
        for pin, name in [(8, 'scl'), (9, 'sda')]:
            if value & (1 << pin):
                setattr(self, name, 1)
            if value & (1 << (pin+16)):
                setattr(self, name, 0)
        # Start/stop are SDA edges while the clock is high, caused by master.
        new_data = self.data_line()
        if self.scl == old_clock == 1 and old_data != new_data:
            if new_data:
                self.mode = 'idle'
                self.slave_low = False
                self.stops += 1
            else:
                self.begin()
        elif self.scl != old_clock and not self.stuck_clock:
            self.edge(bool(self.scl))

    def idr(self):
        return ((1 << 8) if self.scl and not self.stuck_clock else 0) | \
               ((1 << 9) if self.data_line() else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path)
    parser.add_argument('--elf', type=Path, default=ROOT / 'firmware/build/uniknect_c.elf')
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/orientation_20261022/bus_checks.json')
    args = parser.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        segments = [(p['p_vaddr'], p.data()) for p in elf.iter_segments()
                    if p['p_type'] == 'PT_LOAD' and p['p_filesz']]
        syms = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for addr, size in [(0x08000000, 0x200000), (0x20000000, 0x50000),
                       (0x40000000, 0x30000), (0xE000E000, 0x2000)]:
        uc.mem_map(addr, size)
    for addr, data in segments:
        uc.mem_write(addr, data)
    bus = Lis2dh12Bus()
    uc.hook_add(UC_HOOK_MEM_WRITE, lambda m,a,p,s,v,u: bus.bsrr(v), begin=0x40020418, end=0x4002041b)
    uc.hook_add(UC_HOOK_MEM_READ, lambda m,a,p,s,v,u: m.mem_write(0x40020410, struct.pack('<I', bus.idr())),
                begin=0x40020410, end=0x40020413)

    def fault(m, a, size, u):
        raise AssertionError(f'Firmware fault {m.reg_read(UC_ARM_REG_R0):08x}')

    address = syms['board_fault_report'] & ~1
    uc.hook_add(UC_HOOK_CODE, fault, begin=address, end=address)

    def call(name, argument=0):
        uc.reg_write(UC_ARM_REG_SP, 0x2003fff0)
        uc.reg_write(UC_ARM_REG_LR, 0x081ff001)
        uc.reg_write(UC_ARM_REG_R0, argument)
        uc.emu_start(syms[name] | 1, 0x081ff000, count=30_000_000)
        assert uc.reg_read(UC_ARM_REG_SP) == 0x2003fff0, f'{name} failed to return'
        return uc.reg_read(UC_ARM_REG_R0)

    call('app_framework_start')
    assert call('lis2dh12_init') == 0
    assert bus.regs[0x20] == 0x47 and bus.regs[0x23] == 0x88
    assert bus.writes == [(0x20,0x07),(0x21,0),(0x22,0),(0x23,0x88),(0x24,0),(0x25,0),(0x20,0x47)]
    results = ['Actual GPIO I2C START/STOP/ACK/repeated-start and LIS2DH12 configuration: PASS']
    sample = 0x2003e000
    bus.regs[0x27] = 8
    bus.regs[0x28:0x2e] = struct.pack('<hhh', 16000, -16000, 8000)
    assert call('lis2dh12_read', sample) == 0
    assert struct.unpack('<hhh', uc.mem_read(sample,6)) == (1000,-1000,500)
    results.append('Actual six-byte burst / last-byte NACK / signed XYZ mg: PASS')
    bus.regs[0x27] = 0
    uc.mem_write(sample, bytes([0x55])*6)
    assert call('lis2dh12_read', sample) == 1
    assert uc.mem_read(sample,6) == bytes([0x55])*6
    bus.present = False
    assert call('lis2dh12_init') == 2
    bus.present = True
    bus.regs[0x0f] = 0
    assert call('lis2dh12_init') == 3
    bus.regs[0x0f] = 0x33
    bus.stuck_clock = True
    assert call('lis2dh12_init') == 2
    bus.stuck_clock = False
    bus.stuck_data = True
    assert call('lis2dh12_init') == 2
    bus.stuck_data = False
    assert call('lis2dh12_init') == 0  # bus and mutex recovered after every failure
    results.append('Not-ready / absent device / bad identity / stuck SCL/SDA / recovery: PASS')
    assert bus.stops > 0 and bus.starts > bus.stops  # repeated starts do not send STOP
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('\n'.join(results))


if __name__ == '__main__':
    main()
