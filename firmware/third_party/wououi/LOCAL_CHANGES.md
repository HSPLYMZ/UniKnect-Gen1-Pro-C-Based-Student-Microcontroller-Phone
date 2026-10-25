# WouoUI modules used by UniKnect

Upstream: https://github.com/Sheep118/WouoUI-PageVersion
Commit: 82b985a35bca5d1560b3f802350cc507c757d1a9
License: MPL-2.0; see LICENSE. Upstream module source is supplied alongside firmware.

The real upstream graph, ASCII font and integer nonlinear animation modules are
compiled and used by ui_phone.c. This is a modular WouoUI port, not its example
application or a replacement OLED driver. The business/focus state machine is
project-owned. Chinese uses the existing OFL 12x12 font. Local configuration:
160x128 viewport (160x160 scratch buffer also permits rotated diagnostics);
logging disabled. No display pin or SPI init changes.
Any future edits to imported modules must be recorded here and supplied in source.

Graph byte clipping uses x >= width / y >= height to prevent a one-pixel viewport leak.
