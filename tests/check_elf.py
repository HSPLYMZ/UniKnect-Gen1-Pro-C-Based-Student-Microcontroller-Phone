"""Execute BSP diagnostic/clock/heap/lock paths from the linked firmware ELF."""
import argparse
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--deps', type=Path)
    ap.add_argument('--elf', type=Path, default=ROOT / 'firmware/build/uniknect_c.elf')
    ap.add_argument('--expect-modem', action='store_true')
    ap.add_argument('--output', type=Path, default=ROOT / '.tmp/tests/elf_checks.json')
    args = ap.parse_args()
    if args.deps: sys.path.insert(0, str(args.deps.resolve()))
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_MSP, UC_ARM_REG_PSP,
                                  UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_LR,
                                  UC_ARM_REG_PRIMASK)
    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        if elf.get_section_by_name('.symtab').get_symbol_by_name('settings_boot_init'):
            for segment in elf.iter_segments():
                if segment['p_type']=='PT_LOAD' and segment['p_filesz'] and 0x08000000<=segment['p_paddr']<0x08200000:
                    assert segment['p_paddr']+segment['p_filesz']<=0x08140000,'Download segment overlaps persistent settings'
        segments = [(p['p_vaddr'], p.data()) for p in elf.iter_segments()
                    if p['p_type'] == 'PT_LOAD' and p['p_filesz']]
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        dma = elf.get_section_by_name('.symtab').get_symbol_by_name('DMA2_Stream1_IRQHandler')[0]
        expected_binding = 'STB_GLOBAL' if args.expect_modem else 'STB_WEAK'
        assert dma['st_info']['bind'] == expected_binding, 'Unexpected modem DMA IRQ binding'
        lcd_dma = elf.get_section_by_name('.symtab').get_symbol_by_name('DMA2_Stream3_IRQHandler')[0]
        assert lcd_dma['st_info']['bind'] == 'STB_GLOBAL', 'Missing LCD DMA IRQ handler'

    def machine():
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for addr, size in [(0x08000000, 0x200000), (0x20000000, 0x50000),
                           (0x40000000, 0x30000), (0xE000E000, 0x2000)]: uc.mem_map(addr, size)
        for addr, data in segments: uc.mem_write(addr, data)
        uc.reg_write(UC_ARM_REG_SP, 0x2003FFF0)
        return uc

    def word(uc, name): return int.from_bytes(uc.mem_read(symbols[name], 4), 'little')
    def write(uc, addr, value): uc.mem_write(addr, struct.pack('<I', value & 0xFFFFFFFF))
    def call(uc, name, r0=0, r1=0):
        uc.reg_write(UC_ARM_REG_R0, r0 & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_R1, r1 & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_LR, 0x081FF001)
        uc.emu_start(symbols[name] | 1, 0x081FF000, count=1_000_000)
        return uc.reg_read(UC_ARM_REG_R0)

    results = []
    if 'ui_compose_animated' in symbols:
        for name in ['WouoUI_Animation','WouoUI_CanvasDrawStr','WouoUI_CanvasDrawRBoxEmpty']:
            assert name in symbols, f'Actual WouoUI module missing from firmware: {name}'
        results.append('Actual WouoUI graph, font drawing and nonlinear animation linked: PASS')
    uc = machine()
    call(uc, 'board_console_rx_start')
    assert bytes(uc.mem_read(0xE000E400 + 39, 1)) == bytes([0x40]), 'Console RX must preempt the kernel BASEPRI mask'
    results.append('Console RX interrupt priority above kernel mask: PASS')
    for source in ['MSP', 'PSP']:
        for extended in [False, True]:
            uc = machine()
            base = 0x20038000
            frame = [0] * (18 if extended else 0) + [1, 2, 3, 4, 12, 0x08001235, 0x08005678, 0x21000000]
            uc.mem_write(base, struct.pack('<' + 'I' * len(frame), *frame))
            uc.reg_write(UC_ARM_REG_MSP if source == 'MSP' else UC_ARM_REG_PSP, base)
            exc = (0xFFFFFFF9 if source == 'MSP' else 0xFFFFFFFD) & (~0x10 if extended else 0xFFFFFFFF)
            uc.reg_write(UC_ARM_REG_LR, exc)
            stop = symbols['board_fault_report'] & ~1
            uc.hook_add(UC_HOOK_CODE, lambda m, a, s, u: m.emu_stop(), begin=stop, end=stop)
            uc.emu_start(symbols['HardFault_Handler'] | 1, 0, count=10000)
            assert word(uc, 's_fault_pc') == 0x08005678
            assert word(uc, 's_fault_lr') == 0x08001235
            assert word(uc, 's_fault_psr') == 0x21000000
            assert word(uc, 's_fault_exc_return') == exc
            results.append(f'HardFault {source} {"extended" if extended else "basic"}: PASS')

    uc = machine()
    start, limit = symbols['end'], symbols['__libc_heap_limit']
    assert call(uc, '_sbrk') == start
    delta = limit - start
    assert call(uc, '_sbrk', delta) == start
    assert call(uc, '_sbrk', 1) == 0xFFFFFFFF
    assert call(uc, '_sbrk', -0x80000000) == 0xFFFFFFFF
    assert call(uc, '_sbrk', -delta) == limit
    assert call(uc, '_sbrk') == start
    results.append('_sbrk boundary/negative/overflow: PASS')

    for cfgr, pll, expected in [(0, 0, 16000000), (4, 0, 8000000),
                                (8, 8 | (200 << 6) | (1 << 22), 100000000),
                                (8 | (8 << 4), 16 | (200 << 6), 50000000)]:
        write(uc, 0x40023808, cfgr); write(uc, 0x40023804, pll)
        call(uc, 'SystemCoreClockUpdate')
        assert word(uc, 'SystemCoreClock') == expected
    results.append('HSI/HSE/PLL/AHB clock calculation: PASS')

    uc = machine()
    call(uc, '__retarget_lock_acquire_recursive')
    assert word(uc, 'uxSchedulerSuspended') == 0  # before scheduler
    call(uc, '__retarget_lock_release_recursive')
    write(uc, symbols['xSchedulerRunning'], 1)
    call(uc, '__retarget_lock_acquire_recursive')
    call(uc, '__retarget_lock_acquire_recursive')
    assert word(uc, 'uxSchedulerSuspended') == 2
    assert uc.reg_read(UC_ARM_REG_PRIMASK) == 0
    call(uc, '__retarget_lock_release_recursive')
    assert word(uc, 'uxSchedulerSuspended') == 1
    call(uc, '__retarget_lock_release_recursive')
    assert word(uc, 'uxSchedulerSuspended') == 0
    results.append('newlib recursive scheduler locks/interrupt availability: PASS')
    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('\n'.join(results))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
