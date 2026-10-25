"""Compile real firmware C for ARM and run offline in Unicorn; no board access."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--deps', type=Path, help='optional directory containing unicorn/pyelftools')
    ap.add_argument('--output', type=Path, default=ROOT / '.tmp/tests/arm_tests')
    ap.add_argument('--preview-dir', type=Path, help='write software-rendered LCD PNGs (requires Pillow)')
    args = ap.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_INTR
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    temp = output / 'compiler_tmp'
    temp.mkdir(exist_ok=True)
    env = dict(os.environ, TMP=str(temp), TEMP=str(temp))
    inc = ['config', 'bsp', 'app', 'drivers', 'fonts', 'osal', 'third_party/wououi', 'third_party/at_client/include',
           'third_party/cmsis_core/Include', 'third_party/cmsis_device_f4/Include',
           'third_party/FreeRTOS-Kernel/include', 'third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F']
    gcc = Path(os.environ.get('ARM_GCC_ROOT', ROOT / 'tools/arm-gnu-toolchain')) / 'bin/arm-none-eabi-gcc.exe'
    results = []
    for name in ['at', 'uart', 'lcd', 'phone', 'voice', 'voice_service', 'signal', 'signal_service', 'settings', 'settings_store', 'framework', 'adc', 'orientation', 'motion_sensor', 'phone_ui', 'joystick', 'storage', 'navigation', 'no_contact']:
        elf_path = output / f'{name}.elf'
        cmd = [str(gcc), '-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', '-std=gnu11',
               '-O1', '-g', '-DSTM32F413xx', '-ffunction-sections', '-fdata-sections',
               '-Wall', '-Wextra', '-Wno-unused-parameter', '-nostartfiles', '-specs=nano.specs',
               '-Wl,--gc-sections', '-T', str(ROOT / 'tests/arm/test.ld'),
               str(ROOT / f'tests/arm/test_{name}.c'), str(ROOT / 'tests/arm/runtime.c'),
               str(ROOT / 'firmware/app/signal_data.c'), str(ROOT / 'firmware/app/settings_data.c'), '-o', str(elf_path)]
        # Offline fixtures must never inherit a user's private contact header.
        cmd.append('-DCFG_GUARDIAN_PHONE_NUMBER="'+('' if name=='no_contact' else '12345678901')+'"')
        for folder in inc:
            cmd.extend(['-I', str(ROOT / 'firmware' / folder)])
        run = subprocess.run(cmd, capture_output=True, text=True, env=env)
        (output / f'{name}.build.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        if run.returncode:
            print(run.stderr)
            raise SystemExit(run.returncode)
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(0x08000000, 0x200000)
        uc.mem_map(0x20000000, 0x50000)
        uc.mem_map(0x40000000, 0x30000)
        uc.mem_map(0xE000E000, 0x2000)
        with elf_path.open('rb') as f:
            elf = ELFFile(f)
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                    uc.mem_write(segment['p_vaddr'], segment.data())
            syms = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        uc.reg_write(UC_ARM_REG_SP, 0x2004FFF0)
        uc.reg_write(UC_ARM_REG_LR, 0x08000001)
        uc.hook_add(UC_HOOK_INTR, lambda machine, n, user: machine.emu_stop())
        # Four-direction pixel comparisons now include the signal viewport and
        # all 16 pages; keep a finite instruction budget with room for these renders.
        uc.emu_start(syms['test_main'] | 1, 0, timeout=60_000_000,
                     count=1_000_000_000 if name in ('framework','settings_store') else 400_000_000)
        status = int.from_bytes(uc.mem_read(syms['test_status'], 4), 'little', signed=True)
        result={'suite': name, 'status': status}
        if name=='voice_service':
            result['scenarios']=int.from_bytes(uc.mem_read(syms['scenarios'],4),'little')
        results.append(result)
        print(f'{name}: {"PASS" if status == 0 else "FAIL at line/code " + str(status)}')
        if name == 'framework' and status == 0 and args.preview_dir:
            from PIL import Image
            preview_dir = args.preview_dir.resolve()
            preview_dir.mkdir(parents=True, exist_ok=True)
            requests=[(3,'home'),(11,'contact'),(12,'sms'),(13,'contacts'),(6|0x10000,'inbox_empty'),
                      (6,'inbox'),(6|0x400000,'inbox_bottom'),(7,'message'),(8,'call'),(9,'incoming'),(3|0x20000,'offline'),
                      (8|0x40000,'dialing'),(8|0x10000,'ended'),(10,'joystick')]
            requests.extend([(3|0x80000|(n<<20),label) for n,label in enumerate(['nav_select','nav_home','nav_back'])])
            requests.extend([(3|0x1000000|(step<<25),f'slide_{step}') for step in range(1,17)])
            requests.extend([(16,'settings'),(17,'settingsmenu'),(18,'rotation'),(19,'calibration'),(17|0x40000,'settings_failed'),(17|0x200000,'settings_pending')])
            requests.extend([(14,'signal'),(15,'signalinfo'),(15|0x10000,'signal_empty'),
                             (15|0x800000,'signal_bottom'),(15|0x200000,'signal_deferred'),(15|0x40000,'signal_failed')])
            for rotation in range(4):
                for page, label in requests:
                    uc.reg_write(UC_ARM_REG_SP, 0x2004FFF0)
                    uc.reg_write(UC_ARM_REG_LR, 0x081FFF01)
                    uc.reg_write(UC_ARM_REG_R0, page | (rotation<<8))
                    uc.emu_start(syms['test_preview'] | 1, 0x081FFF00, count=60_000_000)
                    assert uc.reg_read(UC_ARM_REG_SP) == 0x2004FFF0, 'Preview did not return'
                    pixels = uc.mem_read(syms['panel'], 160*128*2)
                    rgb = bytearray()
                    for i in range(0, len(pixels), 2):
                        color = int.from_bytes(pixels[i:i+2], 'little')
                        rgb.extend((((color >> 11) & 31)*255//31,
                                    ((color >> 5) & 63)*255//63, (color & 31)*255//31))
                    img = Image.frombytes('RGB', (160, 128), bytes(rgb))
                    angle = rotation*90
                    stem = f'{label}_{angle}'
                    img.resize((640, 512), Image.Resampling.NEAREST).save(preview_dir / f'panel_{stem}.png')
                    # Readable canvas after physically turning the device back upright.
                    upright = img.rotate(angle, expand=True)
                    upright.resize((upright.width*4, upright.height*4), Image.Resampling.NEAREST).save(preview_dir / f'upright_{stem}.png')
                    if rotation == 0:
                        img.resize((640, 512), Image.Resampling.NEAREST).save(preview_dir / f'framework_{label}.png')
    (output / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return 0 if all(r['status'] == 0 for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
