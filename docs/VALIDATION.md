# Public packaging validation — 1.2.1-privacy

## Executed

- The original contact was moved to a separate, ignored local header without changing its value.
- Contact rendering now shows “家人1” without a number row. Matching family call screens use the same name, including +86/0086 forms.
- 19 ARM emulation suites passed for the public source, including a separate empty-contact suite and fictional-number fixtures that bypass private configuration.
- 17 Python tool tests passed.
- Default, offline and LCD diagnostic configurations compiled from this public source tree.
- Linked ELF/BSP checks passed for all three configurations; actual FreeRTOS integration checks passed for the normal configuration.
- Vendored source/font content lock passed, and project Python files parsed successfully.
- C-rendered four-direction previews were regenerated from explicit offline fixtures. Contact and family call previews were visually checked.
- The public release was built with no local number file and uses an empty fixed contact. The release ELF has debug information removed; its loadable program contents remain intact.
- The public directory was scanned for the original number, private ST-LINK identity, personal host paths, private configuration, raw logs, old firmware and flash backups. None are included.

The validation used the existing external Windows Python/toolchain installations.
Build outputs and test caches were placed outside this upload directory. No private
source file is needed by the public build. Linux/macOS and a new machine installation
were not tested.

## Board verification and limits

The public empty-contact image was not flashed. At the owner's separate request,
the privately configured 1.2.1 build was backed up, flashed and verified on their
board. Saved orientation and calibration were preserved. The owner confirmed that
the contact page shows only “家人1”, without its number, and reported normal operation.
No call was initiated by these scripts. Private images, backups and raw board logs
remain outside this repository.

FPS, audio and power-cut acceptance were not rerun for 1.2.1. Earlier 1.2.0 settings
tests are background evidence, not a new public-firmware measurement. Actual caller
IDs are supplied only by a modem on the user's own board; preview values are fixtures.

The public firmware has no family number. To enable a family contact, first copy
and edit `phone_number.local.h`, then rebuild. Such a private firmware must not be
uploaded to a public repository.
