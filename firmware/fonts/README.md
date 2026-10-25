# UniKnect Retro 12 Subset

由 [TakWolf/fusion-pixel-font](https://github.com/TakWolf/fusion-pixel-font) 的
2026.10.16 发布版 `12px monospaced zh_Hans BDF` 生成，字体数据使用 SIL OFL-1.1。
修改后的子集使用本目录的新名称；版权及上游各来源许可完整保留在 `licenses/`。

生成器为 `tools/generate_phone_font.py`。输入缓存位于
`.tmp/phone_ui_20261022/font_zh_hans_12.bdf`；全新环境需从对应 GitHub release 恢复 BDF，
并核对 `FONT_SOURCE.json` 的 BDF SHA256。已使用的 ZIP 不保留，ZIP SHA256 是发布资产摘要。
固件编译直接使用生成的 C，不依赖下载、BDF 或生成器。

共 7,382 字形：包含全部 GB2312 一级汉字 3,755 个、上游具备的二级汉字/符号、
95 个可打印 ASCII 及替代字形。不是完整 Unicode 或完整 GB2312 字库；缺少的
159 个 GB2312 码点逐个记录于 `FONT_SOURCE.json`。不支持的字符显示方框。
ASCII 占 6 像素宽，其他字形占 12 像素宽，每字 12 行；查找表常驻 Flash，
二分查找，无运行时分配或字库装载。字形表占用 191,932 字节。

`phone_font.c`、头文件、清单和许可均纳入 `tools/dependencies.lock.json`。
界面的 5×7 数字、图标、排版和交互逻辑为本项目实现，没有复制 Nokia 商标或字库。
