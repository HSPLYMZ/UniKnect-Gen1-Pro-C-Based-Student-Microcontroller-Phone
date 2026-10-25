# UniKnect Gen1 Pro — C 语言学生单片机电话

[English](README.md) · 当前源码 **1.2.1-privacy**

基于 NUCLEO-F413ZH、QADP-EC200U 和 160×128 ST7735S 屏盾的 C／FreeRTOS 项目。
使用 WouoUI 绘制与动画，五向模拟摇杆操作，界面为白底黑字。

## 编译前必须先配置自己的号码

```powershell
if (-not (Test-Path firmware/config/phone_number.local.h)) {
    Copy-Item firmware/config/phone_number.example.h firmware/config/phone_number.local.h
}
```

编辑 **`firmware/config/phone_number.local.h`**，填写 `CFG_GUARDIAN_PHONE_NUMBER`，再编译烧录。
实际号码只放在这个本地文件中。它已加入 Git 忽略规则，不能上传；更新工程时不要覆盖已有本地文件。

未配置时仍能编译，联系人页显示“暂无联系人”，固定联系人拨号不可用，不会填入任何模拟号码。
配置后联系人列表只显示 **“家人1”**；匹配到固定家人的拨号／通话页也显示名称。
不匹配固定联系人的来电号码沿用现有显示行为。

**使用真实号码编译的固件仍包含号码，不能上传。** `releases/v1.2.1/` 仅提供未配置联系人的公开固件。

## 功能与边界

- 五页循环：数字时钟、家人联系、短信查看、信号参数、系统设置。
- 固定状态栏、功能区、底栏；底栏为选择／主界面／返回。
- 读取模组时间及 LTE 参数；详情快照手动刷新，通话期间暂缓。
- 沿用拨号、接听、拒接和挂断接口；普通上电不会自动拨号。
- 支持四方向显示与引导摇杆标定，配置可掉电保存。
- SPI1＋DMA 保持 6.25 MHz，不改变接线。
- 短信有记录时可阅读，目前尚未实现模组短信拉取服务。
- 不启用邻区测量、自动旋转、SD 日志或语音助手。
- 当前板上没有下行扬声器；历史音频验收为 MIC1 上行。

## Windows 构建

当前验证环境为 Windows，未验证 Linux／macOS。准备 Python 3.13、ARM GNU Toolchain
13.3.rel1（GCC 13.3.1）；烧录另需 STM32CubeProgrammer。

```powershell
py -3.13 -m venv .venv
.venv/Scripts/python.exe -m pip install -r tools/requirements.lock.txt -r tests/requirements.txt
$env:ARM_GCC_ROOT = 'C:\your-tools\arm-gnu-toolchain'
.venv/Scripts/python.exe tools/build.py
```

`ARM_GCC_ROOT` 指向包含 `bin/arm-none-eabi-gcc.exe` 的工具链根目录，也可将工具链放到
`tools/arm-gnu-toolchain/`。工具链与虚拟环境不纳入仓库。
从 [Arm 官方下载页](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 获取固定版本；
历史原始 ZIP 哈希未记录，不伪造来源校验值。安装脚本要求自行提供可信 SHA-256。

应用区为前 1280 KiB，末尾 `0x08140000～0x0817FFFF` 为设置区。
升级保留设置时不要整片擦除；链接与镜像检查会拒绝跨入设置区的应用镜像。

```powershell
# 确认连接正确且无活动电话，再显式执行：
.venv/Scripts/python.exe tools/build.py --flash
.venv/Scripts/python.exe tools/monitor.py -p COM3 -t 10
```

端口、ST-LINK 序列号由使用者按本机情况设置。更多信息见
[接线](docs/HARDWARE.md)、[操作](docs/OPERATION.md)、[测试](tests/README.md)、
[验证范围](docs/VALIDATION.md) 与 [实际 C 渲染预览](docs/preview.html)。
预览使用离线夹具，不能作为实板画面或帧率证明。

## 许可证

自研代码使用 MIT。第三方组件保留各自许可；WouoUI 相关文件仍为 MPL-2.0，字库保留 OFL 及原版权。
详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
