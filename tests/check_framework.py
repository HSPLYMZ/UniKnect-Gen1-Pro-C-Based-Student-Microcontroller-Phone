"""Exercise the real linked framework/FreeRTOS queues and BSP UART in Unicorn.

This does not emulate peripherals or prove scheduler timing on the board.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path)
    parser.add_argument('--elf', type=Path, default=ROOT / 'firmware/build/uniknect_c.elf')
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/framework_20261022/integration.json')
    args = parser.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    from elftools.elf.elffile import ELFFile
    from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                         UC_HOOK_CODE, UC_HOOK_MEM_READ)
    from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0,
                                  UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PRIMASK)

    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        segments = [(p['p_vaddr'], p.data()) for p in elf.iter_segments()
                    if p['p_type'] == 'PT_LOAD' and p['p_filesz']]
        symtab = elf.get_section_by_name('.symtab')
        syms = {s.name: s['st_value'] for s in symtab.iter_symbols()}
        getc_size = symtab.get_symbol_by_name('board_console_getc')[0]['st_size']
        assert symtab.get_symbol_by_name('USART3_IRQHandler')[0]['st_info']['bind'] == 'STB_GLOBAL'
        modem_present='at_probe_start' in syms
        assert symtab.get_symbol_by_name('USART6_IRQHandler')[0]['st_info']['bind'] == ('STB_GLOBAL' if modem_present else 'STB_WEAK')
        assert 'phone_probe_start' not in syms

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in [(0x08000000, 0x200000), (0x20000000, 0x50000),
                          (0x40000000, 0x30000), (0xE000E000, 0x2000)]:
        uc.mem_map(address, size)
    for address, data in segments:
        uc.mem_write(address, data)

    def fault(machine, address, size, user):
        raise AssertionError(f'Firmware fault {machine.reg_read(UC_ARM_REG_R0):08x}')

    stop = syms['board_fault_report'] & ~1
    uc.hook_add(UC_HOOK_CODE, fault, begin=stop, end=stop)

    def put(address, value):
        uc.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def word(name):
        return int.from_bytes(uc.mem_read(syms[name], 4), 'little')

    def call(name, a=0, b=0, c=0, d=0, stack=()):
        uc.reg_write(UC_ARM_REG_SP, 0x2003fff0)
        if stack:
            uc.mem_write(0x2003fff0,struct.pack('<'+'I'*len(stack),*stack))
        for reg, value in [(UC_ARM_REG_R0, a), (UC_ARM_REG_R1, b), (UC_ARM_REG_R2, c), (UC_ARM_REG_R3, d)]:
            uc.reg_write(reg, value & 0xffffffff)
        uc.reg_write(UC_ARM_REG_LR, 0x081ff001)
        uc.emu_start(syms[name] | 1, 0x081ff000, count=5_000_000)
        assert uc.reg_read(UC_ARM_REG_SP) == 0x2003fff0, f'{name} did not return'
        return uc.reg_read(UC_ARM_REG_R0)

    results = []
    call('app_framework_start')
    count=9
    active=[0,2,3,4,5,8]
    assert call('app_task_active_count') == len(active)
    assert word('uxCurrentNumberOfTasks') == len(active)
    handles = struct.unpack('<9I', uc.mem_read(syms['s_handles'], 4*count))
    assert len(set(handles[i] for i in active)) == len(active)
    assert all(0x20000000 <= handles[i] < 0x20040000 for i in active)
    assert all(handles[i]==0 for i in [1,6,7])
    assert word('s_events') and word('s_logs')
    # Optional modem peripheral clocks must not be enabled by framework startup.
    assert not int.from_bytes(uc.mem_read(0x40023844, 4), 'little') & (1 << 5)  # USART6
    assert not int.from_bytes(uc.mem_read(0x40023830, 4), 'little') & (1 << 22)  # DMA2
    results.append('Ordered initialization / six active static tasks / light-motion-SD disabled: PASS')

    scratch = 0x2003e000
    # Payload size from the linked firmware's app_event_t DWARF, including SMS.
    queue = word('s_events')
    type_sizes={}; phone_offsets={}; members={}
    # app_event_t is anonymous in DWARF; resolve its typedef instead.
    with args.elf.open('rb') as stream:
        dwarf=ELFFile(stream).get_dwarf_info()
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag=='DW_TAG_typedef' and die.attributes.get('DW_AT_name'):
                    name=die.attributes['DW_AT_name'].value
                    if name in (b'app_event_t',b'phone_intent_t',b'voice_delivery_t',b'phone_state_t',b'signal_state_t',b'signal_sample_t',b'app_model_t',b'settings_state_t'):
                        target=die.get_DIE_from_attribute('DW_AT_type')
                        type_sizes[name]=target.attributes['DW_AT_byte_size'].value
                        members[name]={m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value
                                       for m in target.iter_children() if m.tag=='DW_TAG_member'}
                        if name==b'phone_state_t':
                            phone_offsets={m.attributes['DW_AT_name'].value.decode():m.attributes['DW_AT_data_member_location'].value
                                           for m in target.iter_children() if m.tag=='DW_TAG_member'}
            if len(type_sizes)==8:
                break
    event_size=type_sizes[b'app_event_t']
    assert event_size and event_size>=280
    assert type_sizes[b'phone_intent_t']==32 and type_sizes[b'voice_delivery_t']==48
    payload=struct.pack('<I',10)+bytes((i*31+7)&255 for i in range(event_size-4))
    uc.mem_write(scratch,payload)
    for _ in range(24):
        assert call('app_event_post', scratch) == 1
    assert call('app_event_post', scratch) == 0 and call('app_event_drops') == 1
    for _ in range(24):
        assert call('xQueueReceive', queue, scratch + 0x800, 0) == 1
        assert uc.mem_read(scratch + 0x800, event_size) == payload
    assert call('xQueueReceive', queue, scratch + 0x800, 0) == 0
    results.append(f'Real FreeRTOS event queue / {event_size}-byte phone payload / full-drop / drain: PASS')

    # Exercise request admission and mailbox APIs on the actual linked FreeRTOS
    # queues while the ordinary event queue is full. The completion below is an
    # injected producer boundary, not a simulated successful modem command.
    for _ in range(24):
        assert call('app_event_post', scratch) == 1
    intent = bytes([1]) + b'10086\0' + bytes(25)  # 32-byte phone_intent_t, generation=0
    uc.mem_write(scratch + 0x200, intent)
    request_id = call('voice_service_submit', scratch + 0x200)
    assert request_id and call('voice_service_submit', scratch + 0x200) == 0
    assert call('voice_service_busy') == 1
    assert call('xQueueReceive', word('s_queue'), scratch + 0x400, 0) == 1
    assert int.from_bytes(uc.mem_read(scratch + 0x400, 4), 'little') == request_id
    # voice_delivery_t: revision, generation, result_id, 32-byte call, valid,note,pad.
    mailbox = struct.pack('<III', 1, 0, request_id) + bytes(32) + bytes([0, 5, 0, 0])
    uc.mem_write(syms['s_delivery'], mailbox)
    assert call('voice_service_submit', scratch + 0x200) == 0
    phone=scratch+0xa00; page=scratch+0x600; revision_ptr=scratch+0x604
    assert phone+type_sizes[b'phone_state_t']<0x2003f800
    call('phone_state_init',phone)
    uc.mem_write(phone+phone_offsets['pending'],b'\x01')
    put(phone+phone_offsets['pending_id'],request_id)
    put(page,3);put(revision_ptr,0)
    # AAPCS: the aligned uint64_t fourth parameter and revision pointer use stack.
    call('voice_service_apply',phone,page,0,stack=(0,0,revision_ptr))
    assert uc.mem_read(phone+phone_offsets['pending'],1)==b'\0'
    assert uc.mem_read(phone+phone_offsets['note'],1)==b'\x05'
    assert call('voice_service_busy') == 0
    assert int.from_bytes(uc.mem_read(syms['s_delivery']+8,4),'little')==0
    call('voice_service_apply',phone,page,0,stack=(0,0,revision_ptr))
    assert int.from_bytes(uc.mem_read(revision_ptr,4),'little')==2
    for _ in range(24):
        assert call('xQueueReceive', queue, scratch + 0x800, 0) == 1
    results.append('Real FreeRTOS request admission / retained mailbox / full event queue / matching ack: PASS')

    # Signal completion injection tests the producer boundary on the linked
    # production mailbox, with the real ordinary event queue saturated. No AT
    # success is simulated here; command and parsing paths have separate tests.
    for _ in range(24):
        assert call('app_event_post', scratch) == 1
    drops=call('app_event_drops')
    call('signal_service_init');call('phone_state_init',phone)
    signal=phone+phone_offsets['signal'];so=members[b'signal_state_t']
    sample_size=type_sizes[b'signal_sample_t']
    request_id=call('signal_service_request')
    assert request_id and call('signal_service_request')==request_id
    put(signal+so['request_id'],request_id)
    # Static mailbox: sample, next ID, reserved ID, revision, state.
    mailbox=syms['s_signal'];put(mailbox+sample_size+8,2)
    uc.mem_write(mailbox+sample_size+12,b'\x04')  # retained DONE
    uc.mem_write(mailbox+members[b'signal_sample_t']['present'],b'\x01')
    assert call('signal_service_request')==request_id
    assert call('signal_service_pending')==0
    call('signal_service_apply',phone,0,1000,0,stack=(0,))
    assert int.from_bytes(uc.mem_read(signal+so['completed_id'],4),'little')==request_id
    assert int.from_bytes(uc.mem_read(signal+so['request_id'],4),'little')==0
    assert int.from_bytes(uc.mem_read(mailbox+sample_size+4,4),'little')==0
    call('signal_service_apply',phone,0,2000,0,stack=(0,))
    assert call('app_event_drops')==drops
    assert call('signal_service_request')!=request_id
    for _ in range(24):
        assert call('xQueueReceive', queue, scratch+0x800,0)==1
    assert uc.reg_read(UC_ARM_REG_PRIMASK)==0
    results.append('Real FreeRTOS full event queue / signal result retained / matching consumption / no extra drops: PASS')

    # The production calibration owner and controller deliver terminal results
    # without posting to the saturated event queue.
    for _ in range(24):
        assert call('app_event_post',scratch)==1
    drops=call('app_event_drops')
    model=0x20038000;joy=scratch+0x400
    assert model+type_sizes[b'app_model_t']<0x2003d000
    call('app_model_init',model);call('settings_input_init',joy)
    assert call('settings_action',model,147)==1  # explicit host calibration
    system=model+members[b'app_model_t']['phone']+phone_offsets['system']
    cal=members[b'settings_state_t']['cal_id']
    request_id=int.from_bytes(uc.mem_read(system+cal,4),'little');assert request_id
    call('settings_input_step',joy,4065,1,0,stack=(0,))
    assert call('settings_action',model,145)==1  # cancel
    call('settings_input_step',joy,4065,1,5,stack=(0,))
    assert int.from_bytes(uc.mem_read(syms['s_cal']+4,4),'little')==request_id
    call('settings_service_apply',model,0)
    assert int.from_bytes(uc.mem_read(system+cal,4),'little')==0
    assert int.from_bytes(uc.mem_read(syms['s_cal']+4,4),'little')==0
    assert call('app_event_drops')==drops
    for _ in range(24):
        assert call('xQueueReceive',queue,scratch+0x800,0)==1
    results.append('Real FreeRTOS full event queue / calibration cancel retained / restored input / matching ack: PASS')

    uc.mem_write(scratch, b'complete log record\0' + bytes(200))
    assert call('board_log_enqueue', scratch) == 0  # pre-scheduler fallback
    put(syms['xSchedulerRunning'], 1)
    for _ in range(16):
        assert call('board_log_enqueue', scratch) == 1
    assert call('board_log_enqueue', scratch) == 1 and call('app_log_drops') == 1
    queue = word('s_logs')
    for _ in range(16):
        assert call('xQueueReceive', queue, scratch + 256, 0) == 1
        assert uc.mem_read(scratch + 256, 20) == b'complete log record\0'
    call('vTaskSuspendAll')
    assert call('board_log_enqueue', scratch) == 1  # remains queued while suspended
    assert call('xQueueReceive', queue, scratch + 256, 0) == 1
    call('xTaskResumeAll')
    results.append('Atomic log records / queue saturation / suspended scheduler: PASS')

    put(syms['xTickCount'], 40001)
    info = scratch + 512
    call('app_task_snapshot', info)
    # 32-bit ABI: pointer, age, beats, two uint16 values, priority, stale, padding.
    rows = [struct.unpack('<IIIHHBBB1x', uc.mem_read(info + i*20, 20)) for i in range(count)]
    assert all(rows[i][6] == 1 and 64 < rows[i][3] < rows[i][4] and rows[i][7] for i in active)
    assert all(rows[i][6]==0 and rows[i][7]==0 for i in [1,6,7])
    assert rows[4][5] >= rows[3][5], 'Console must run while DMA releases the display task'
    put(syms['xTickCount'], 0xfffffff0)
    for i in range(count):
        call('app_task_beat', i)
    put(syms['xTickCount'], 0x20)
    call('app_task_snapshot', info)
    rows = [struct.unpack('<IIIHHBBB1x', uc.mem_read(info + i*20, 20)) for i in range(count)]
    assert all(row[1] == 48 and row[6] == 0 and row[2] == 1 for row in rows)
    results.append('Task stack watermark / expired heartbeat / tick wrap: PASS')

    usart = 0x40004800

    def receive(byte, status=1 << 5):
        put(usart, status)
        put(usart + 4, byte)
        call('USART3_IRQHandler')

    assert call('board_console_getc') == 0xffffffff
    for i in range(1024):
        receive(i)
    receive(99)
    assert call('board_console_rx_drops') == 1
    assert call('board_console_getc') == 0xfffffffe  # loss marker and pending flush
    assert call('board_console_getc') == 0xffffffff
    for i in range(1024):
        receive(i)
    for i in range(1024):
        assert call('board_console_getc') == (i & 0xff)
    assert call('board_console_getc') == 0xffffffff
    # Exercise uint16 producer/consumer wrap, not just array-index wrap.
    for name in ['s_console_rx_head', 's_console_rx_tail']:
        uc.mem_write(syms[name], struct.pack('<H', 65530))
    for i in range(20):
        receive(65+i)
    for i in range(20):
        assert call('board_console_getc') == 65+i
    receive(88, (1 << 5) | (1 << 1))  # framing error is rejected
    assert call('board_console_getc') == 0xfffffffe
    assert call('board_console_getc') == 0xffffffff
    assert call('board_console_rx_drops') == 2
    results.append('UART RX ISR / full ring / 16-bit wrap / corrupt byte: PASS')

    # Pend RXNE|ORE with LF immediately after getc reads the loss flag. The
    # Cortex-M model below delivers the ISR's visible writes only once PRIMASK
    # allows it. Before the fix this window returned LF before the loss marker,
    # potentially accepting a truncated command at the line collector.
    for name in ['s_console_rx_head', 's_console_rx_tail']:
        uc.mem_write(syms[name], bytes(2))
    uc.mem_write(syms['s_console_rx_loss'], b'\0')
    uc.reg_write(UC_ARM_REG_PRIMASK, 0)
    race = {'pending': False, 'armed': True, 'masked': False, 'delivered': False}

    def pend_rx_error(machine, access, address, size, value, user):
        if race['armed']:
            race['armed'] = False
            race['pending'] = True
            race['masked'] = bool(machine.reg_read(UC_ARM_REG_PRIMASK))

    def deliver_rx_error(machine, address, size, user):
        if race['pending'] and not machine.reg_read(UC_ARM_REG_PRIMASK):
            # Same ring/loss effects as USART3_IRQHandler for RXNE|ORE.
            machine.mem_write(syms['s_console_rx'], b'\n')
            machine.mem_write(syms['s_console_rx_head'], struct.pack('<H', 1))
            machine.mem_write(syms['s_console_rx_loss'], b'\1')
            race['pending'] = False
            race['delivered'] = True

    start = syms['board_console_getc'] & ~1
    # Earlier checks already translated this function. Retranslate it so the
    # newly added memory-read hook also observes the cached loss-flag load.
    uc.ctl_remove_cache(start, start + getc_size)
    read_hook = uc.hook_add(UC_HOOK_MEM_READ, pend_rx_error,
                           begin=syms['s_console_rx_loss'], end=syms['s_console_rx_loss'])
    code_hook = uc.hook_add(UC_HOOK_CODE, deliver_rx_error,
                           begin=start, end=start + getc_size - 1)
    assert call('board_console_getc') == 0xffffffff  # empty at transaction start
    uc.hook_del(read_hook)
    uc.hook_del(code_hook)
    assert race['masked'] and race['delivered'] and not race['pending']
    assert call('board_console_getc') == 0xfffffffe  # discard pending LF first
    assert call('board_console_getc') == 0xffffffff

    # All three return paths must restore a pre-existing interrupt mask.
    for initial_mask in [0, 1]:
        uc.reg_write(UC_ARM_REG_PRIMASK, initial_mask)
        assert call('board_console_getc') == 0xffffffff
        assert uc.reg_read(UC_ARM_REG_PRIMASK) == initial_mask
        receive(65)
        assert call('board_console_getc') == 65
        assert uc.reg_read(UC_ARM_REG_PRIMASK) == initial_mask
        receive(88, (1 << 5) | (1 << 1))
        assert call('board_console_getc') == 0xfffffffe
        assert uc.reg_read(UC_ARM_REG_PRIMASK) == initial_mask
    uc.reg_write(UC_ARM_REG_PRIMASK, 0)
    results.append('UART loss/byte atomicity / instruction-gap RX error / PRIMASK restoration: PASS')

    out = args.output.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('\n'.join(results))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
