# Third-party notices

The root MIT license applies only to original project code. It does not replace
existing notices or relicense third-party files, generated fonts, or modified
third-party components. Retain each component's notices when redistributing it.

| Component | License and source |
|---|---|
| FreeRTOS-Kernel | MIT; `firmware/third_party/FreeRTOS-Kernel/LICENSE.md` |
| ARM CMSIS core | Apache-2.0; `firmware/third_party/cmsis_core/LICENSE` and original file headers |
| ST CMSIS device / HAL | Original ST licenses in each directory and source file |
| Quectel / RT-Thread AT client | Apache-2.0; `firmware/third_party/at_client/LICENSE`, original headers and `LOCAL_CHANGES.md` |
| WouoUI | MPL-2.0; `firmware/third_party/wououi/LICENSE`, upstream identity and local patch notes |
| UniKnect Retro 12 Subset | OFL-1.1 and inherited notices under `firmware/fonts/licenses/`; see font README and `FONT_SOURCE.json` |

Sources and recorded versions are in `tools/dependencies.lock.json`. Where the
original upstream commit was not recorded it remains unknown. Local content
hashes are integrity records, not claims of publisher authentication.
