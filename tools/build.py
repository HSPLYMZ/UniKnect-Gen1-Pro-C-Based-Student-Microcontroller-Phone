"""Configure + build the firmware with the in-project toolchain.

Usage (from anywhere):
    .venv/Scripts/python.exe tools/build.py            # configure if needed, then build
    .venv/Scripts/python.exe tools/build.py --clean    # wipe build/ first
    .venv/Scripts/python.exe tools/build.py --flash    # build then flash via ST-LINK
    .venv/Scripts/python.exe tools/build.py --lcd-diagnostic  # color bars
    .venv/Scripts/python.exe tools/build.py --no-modem  # offline build
"""
import os
import argparse
import shutil
import subprocess
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware")
BUILD = os.path.join(FW, "build")
def executable(name):
    for parent in (os.path.join(ROOT,".venv","Scripts"),os.path.dirname(sys.executable)):
        candidate=os.path.join(parent,name+".exe")
        if os.path.isfile(candidate):return candidate
    found=shutil.which(name)
    if not found:raise RuntimeError("Install "+name+" from tools/requirements.lock.txt")
    return found
CMAKE = executable("cmake")
NINJA = executable("ninja")
TOOLCHAIN = os.path.join(FW, "cmake", "arm-none-eabi.cmake")
PROGRAMMER = r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"


def run(cmd, cwd=None):
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd or ROOT, check=True)


def validate_flash_image(path):
    """Keep every downloadable flash segment below the persistent sectors."""
    with open(path,'rb') as stream:
        data=stream.read()
    if len(data)<52 or data[:6]!=b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32 firmware')
    offset=struct.unpack_from('<I',data,28)[0]
    size,count=struct.unpack_from('<HH',data,42)
    if size!=32 or not count or offset+size*count>len(data):
        raise ValueError('Invalid ELF program headers')
    flash_segments=0
    for i in range(count):
        kind,file_offset,virtual,physical,length,memory,flags,alignment=struct.unpack_from('<8I',data,offset+i*size)
        if kind!=1 or not length:
            continue
        if file_offset+length>len(data):
            raise ValueError('Truncated ELF segment')
        if 0x08000000<=physical<0x08200000:
            if physical+length>0x08140000:
                raise ValueError('Firmware overlaps persistent settings sectors')
            flash_segments+=1
    if not flash_segments:
        raise ValueError('No application flash segment')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clean', action='store_true')
    parser.add_argument('--flash', action='store_true')
    parser.add_argument('--lcd-diagnostic', action='store_true', help='color bars instead of application pages')
    parser.add_argument('--modem-diagnostic', action='store_true', help='explicitly enable the AT diagnostic service')
    parser.add_argument('--no-modem', action='store_true', help='disable the AT and phone services')
    args = parser.parse_args(argv)
    if args.no_modem and args.modem_diagnostic:
        parser.error('--no-modem and --modem-diagnostic conflict')
    clean, flash = args.clean, args.flash
    global BUILD
    BUILD = os.path.abspath(os.environ.get("UNIKNECT_BUILD_DIR", BUILD))

    if clean and os.path.isdir(BUILD):
        resolved_root, resolved_build = os.path.realpath(ROOT), os.path.realpath(BUILD)
        expected_build = os.path.join(resolved_root, 'firmware', 'build')
        if os.path.normcase(resolved_build) != os.path.normcase(expected_build):
            raise RuntimeError('Refusing to clean a redirected build directory')
        shutil.rmtree(BUILD)

    configure = [CMAKE, "-S", FW, "-B", BUILD, "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_MAKE_PROGRAM={NINJA}",
                 f"-DPython3_EXECUTABLE={sys.executable}",
                 "-DAPP_PHONE_ONLY=ON",
                 f"-DAPP_MODEM_ENABLE={'ON' if (not args.no_modem and (not args.lcd_diagnostic or args.modem_diagnostic)) else 'OFF'}",
                 f"-DAPP_LCD_DIAGNOSTIC={'ON' if args.lcd_diagnostic else 'OFF'}",
                 "-DPHONE_PROBE_ENABLE=OFF"]
    if not os.path.isfile(os.path.join(BUILD, "CMakeCache.txt")):
        configure.append(f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}")
    run(configure)

    run([CMAKE, "--build", BUILD])

    if flash:
        elf = os.path.join(BUILD, "uniknect_c.elf")
        if not os.path.isfile(elf):
            print("elf not found:", elf)
            return 1
        if not os.path.isfile(PROGRAMMER):
            print("STM32_Programmer_CLI not found:", PROGRAMMER)
            return 1
        validate_flash_image(elf)
        run([PROGRAMMER, "-c", "port=SWD", "reset=HWrst", "-w", elf, "-v", "-rst"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
