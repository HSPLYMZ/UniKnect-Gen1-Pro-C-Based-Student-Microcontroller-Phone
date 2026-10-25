# Operation / 操作

1. Top pages cycle left/right: clock, family, SMS, signal, settings.
2. Down enters the footer, initially Select. Left/right stops at its edges; Up restores content focus.
3. Select confirms the content item. Home returns to the clock and clears history; Back restores the parent.
4. Each physical press acts once; fully release before the next press. No long-press shortcuts or repeat.

## Family / 家人联系

Before building, copy `firmware/config/phone_number.example.h` to
`firmware/config/phone_number.local.h` and fill your own number. Never upload
that file or a firmware image compiled with it.

The entry opens the contact list; confirming “家人1” is the explicit call action.
Opening the entry or navigating Home/Back does not dial or end a call.
The contact list does not draw the number. Matching family call screens draw
“家人1”, including the module's +86/0086 form; other incoming caller IDs are unchanged.
Without a configured contact, the list shows “暂无联系人”.

## Settings / 系统设置

Select one of four display directions and confirm to apply and save. Guided
calibration first asks for release, then Up, Down, Left, Right, Center. Release
between steps. It returns to settings after success. An incoming call, explicit
cancel, or 60 seconds without progress restores the previous calibration.

Rotation and calibration are persisted using CRC-checked, committed 64-byte
records in the last two flash sectors. Runtime code appends words only;
reclamation runs before the scheduler and AT services start. Flash writes are
deferred during calls. Failed saves retain the last committed configuration.
The serial `rotate` command is temporary and does not change the saved direction.

## Signal / 信号参数

Entering or explicitly refreshing captures serving-cell and operator data.
Background status-bar updates do not replace this snapshot. Queries are deferred
during calls. SINR is the module's raw code converted as `(raw - 47) / 2 dB`,
not an independently calibrated RF measurement. Neighbor-cell queries are disabled.

## Diagnostics

`status`, `tasks`, `page settings`, `settings status`, `settings retry`,
`joy calibrate`, `joy cancel`, `page signalinfo`, `signal refresh`, `signal status`.
`key call/end/delete` are explicit diagnostics with real business effects; do not
use them as navigation commands. Board test scripts must be run deliberately.
