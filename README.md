# UniKnect Gen1 Pro — C-Based Student Microcontroller Phone

[简体中文](README.zh-CN.md) · Source version **1.2.1-privacy**

A student microcontroller phone built with C and FreeRTOS on the NUCLEO-F413ZH,
QADP-EC200U and a 160×128 ST7735S LCD shield. The monochrome UI uses WouoUI drawing
and animations with a five-way analog joystick.

## Configure your phone number before building

```powershell
if (-not (Test-Path firmware/config/phone_number.local.h)) {
    Copy-Item firmware/config/phone_number.example.h firmware/config/phone_number.local.h
}
```

Edit **`firmware/config/phone_number.local.h`** and set `CFG_GUARDIAN_PHONE_NUMBER`
to your own family contact number. Then rebuild and flash. This local file is
ignored by Git and must never be uploaded. Do not overwrite an existing local
file when updating the project.

With no configured number, the firmware builds normally, shows “暂无联系人”
(no contacts), and disables the fixed contact dial action. It never substitutes
a demonstration number. The configured contact is displayed as **“家人1”**
(Family 1), including matching family call screens, rather than its number.
Incoming caller IDs that do not match the family contact retain their existing display.

**A firmware image built with a real number contains that number.** Only the
unconfigured public images in [releases/v1.2.1](releases/v1.2.1) belong in a public repository.

## Features and limits

- Five pages: clock → family contact → SMS viewer → signal parameters → settings.
- Fixed status/content/navigation regions; Select / Home / Back footer.
- LTE date/time, RSRP, SINR, RSRQ, RSSI, band, cell ID and operator snapshots.
- Existing EC200U voice calling, answering, rejection and hangup interfaces.
- Four display orientations and guided joystick calibration, with power-off persistence.
- SPI1 + DMA at the verified **6.25 MHz**; no wiring changes or automatic calls.
- SMS records can be displayed, but a modem SMS retrieval service is not implemented.
- Neighbor-cell measurement, automatic rotation, SD logging and assistant features are not enabled.
- Board-side audio playback is not provided by the current hardware; previous voice tests used MIC1 uplink.

## Build on Windows

Windows is the currently supported and verified host. Linux/macOS are not verified.
Install Python 3.13, ARM GNU Toolchain **13.3.rel1 / GCC 13.3.1**, and optionally
STM32CubeProgrammer for flashing. Toolchain binaries and virtual environments are not committed.

```powershell
py -3.13 -m venv .venv
.venv/Scripts/python.exe -m pip install -r tools/requirements.lock.txt -r tests/requirements.txt
# Set to the installation ROOT, containing bin/arm-none-eabi-gcc.exe:
$env:ARM_GCC_ROOT = 'C:\your-tools\arm-gnu-toolchain'
.venv/Scripts/python.exe tools/build.py
```

Alternatively install the toolchain under `tools/arm-gnu-toolchain/`.
Obtain it from [Arm](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
The historical download archive hash was not recorded; the repository does not invent one.
`tools/setup_toolchain.py` requires a trusted archive SHA-256 supplied by the user.

The application occupies the first 1280 KiB of flash. The last two 128 KiB sectors,
`0x08140000–0x0817FFFF`, store settings. The linker and flash-image validator protect them.
Do not mass-erase the device when you want to retain settings.

```powershell
# Only when the correct board is connected and no call is active:
.venv/Scripts/python.exe tools/build.py --flash
.venv/Scripts/python.exe tools/monitor.py -p COM3 -t 10
```

Ports and ST-LINK identities are local choices, not project requirements.
See [hardware](docs/HARDWARE.md), [operation](docs/OPERATION.md),
[tests](tests/README.md), and [public validation](docs/VALIDATION.md).

## Preview and project layout

[Actual C rendering gallery](docs/preview.html) uses offline fixtures, not board captures.

| Directory | Contents |
|---|---|
| `firmware/` | Application, FreeRTOS integration, drivers, fonts and pinned dependencies |
| `tools/` | Build, diagnostic, font and preview tools |
| `tests/` | ARM emulation, linked ELF checks, tool tests and explicit board scripts |
| `docs/` | Public hardware, usage, architecture and validation notes |
| `releases/v1.2.1/` | Public firmware without a configured contact, plus checksums |

This repository excludes private configuration, original flash backups, raw modem logs,
development environments and the former development workspace.

## License

Original project code is MIT licensed. Existing third-party and font notices remain
in force; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). WouoUI-covered files
remain MPL-2.0, and the generated font retains its OFL notices.
