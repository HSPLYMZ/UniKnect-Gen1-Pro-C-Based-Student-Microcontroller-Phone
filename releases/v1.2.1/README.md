# Public firmware — 1.2.1-privacy

These images were rebuilt from this public source with **no configured family
number**. The contact page shows “暂无联系人”. Configure the ignored local number
header and rebuild to use “家人1”; never publish the resulting private image.

- `uniknect_c.elf`: flashable ELF, debug information removed.
- `uniknect_c.bin`: application binary at `0x08000000`.
- `uniknect_c.hex`: Intel HEX application image.
- `uniknect_c.map`: link map with private host paths removed.
- `SHA256SUMS.txt`: integrity checks for the four application artifacts.

Do not mass-erase if you want to keep the last two settings sectors. These public
images were not flashed; the owner's private build was verified separately. See
`../../docs/VALIDATION.md` for the checks actually executed. This directory contains
no settings-sector or full-flash backup.
