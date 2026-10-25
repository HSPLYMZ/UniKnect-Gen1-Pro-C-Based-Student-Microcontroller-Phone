# Build and diagnostic tools

The main README contains the Windows setup and mandatory private-number step.
Toolchain executables and virtual environments are installed locally, not shipped.

- `build.py`: CMake/Ninja build, optional explicit `--flash`; rejects flash download segments entering the settings region.
- `check_dependencies.py`: checks repository-managed third-party, font and Python requirement bytes.
- `setup_toolchain.py`: installs a ZIP only after a caller-supplied trusted SHA-256 check.
- `monitor.py`, `probe_modem.py`: explicit serial diagnostics; modem responses may contain private device data, so do not upload logs.
- `render_phone_preview.py`: actual ARM C frames and offline-fixture gallery.
- `generate_phone_font.py`: optional font regeneration using a separately obtained, hash-checked BDF. Normal builds use the included C font.

`ARM_GCC_ROOT` selects an external toolchain root. If unset,
`tools/arm-gnu-toolchain/` is used. `UNIKNECT_BUILD_DIR` can place build artifacts
outside the source checkout. The default is `firmware/build/`. Python, CMake and
Ninja are located from the current environment or local `.venv`.

```powershell
.venv/Scripts/python.exe tools/render_phone_preview.py --output docs/images/preview --html docs/preview.html
```

The gallery is not an actual board capture. Ports shown in examples are only
examples; use the correct devices on your machine.
