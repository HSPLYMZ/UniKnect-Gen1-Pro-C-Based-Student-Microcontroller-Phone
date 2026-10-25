# Tests

Install `tools/requirements.lock.txt` and `tests/requirements.txt` in your Python
environment, and configure `ARM_GCC_ROOT` as described in the main README.

```powershell
.venv/Scripts/python.exe tools/check_dependencies.py
.venv/Scripts/python.exe tests/run_arm_tests.py --output .tmp/arm_tests --preview-dir .tmp/previews
.venv/Scripts/python.exe tests/test_tools.py
.venv/Scripts/python.exe tests/check_elf.py --expect-modem --output .tmp/elf_checks.json
.venv/Scripts/python.exe tests/check_framework.py --output .tmp/framework_checks.json
```

ARM emulation tests use an explicitly supplied fictional number, not your private
header. The `no_contact` suite separately verifies empty configuration and that
incoming-call answering is not tied to having a fixed contact.

The suites cover the production voice and signal services, navigation, input,
DMA/cache paths, persistent settings, interrupted writes, CRC/format failures,
calibration cancellation and incoming-call races. Emulation does not establish
physical frame rate, RF calibration, audio quality or actual power-loss behavior.

Board scripts under `tests/board/` use explicit ports and versions. Some save
orientation and reset the MCU. Read each script before running it; use a board
with no active call. They do not run automatically as part of offline tests.

```powershell
.venv/Scripts/python.exe tests/board/check_settings.py --port COM3 --stlink-serial YOUR_STLINK_SERIAL
.venv/Scripts/python.exe tools/benchmark_lcd.py --port COM3 --seconds 60 --angles 0 90 180 270 --scenes settings --min-fps 30
```

No real telephone operation is part of the public packaging validation.
