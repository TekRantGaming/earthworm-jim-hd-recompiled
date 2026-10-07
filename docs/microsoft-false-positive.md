# Microsoft false-positive submission

Submit at https://www.microsoft.com/en-us/wdsi/filesubmission, signed in with your Microsoft account.

- **Submit as:** Software developer
- **Product:** Microsoft Defender Antivirus (Windows 10/11)
- **File:** `rexruntime.dll` from `EarthwormJimHD-v0.9.3-windows-x64.zip` (unzip it first; upload the DLL itself)
- **What do you believe this file is?** Incorrectly detected as malware/malicious
- **Detection name:** `Trojan:Win32/Wacatac.B!ml`
- **Definition version:** shown in Windows Security > Protection history (optional)

**Additional information** (paste):

> This file is `rexruntime.dll`, the runtime library of the ReXGlue SDK v0.10.0, an open-source toolkit for statically recompiling Xbox 360 games (https://github.com/rexglue/rexglue-sdk). It is shipped unmodified as part of my open-source PC port of Earthworm Jim HD (https://github.com/TekRantGaming/earthworm-jim-hd-recompiled), and it is byte-for-byte identical to the file in the SDK's official v0.10.0 release (SHA-256 e359209fb2b0570e693c966d4c1d99a82465d36ef70d033833fae56adb2f1b7a).
>
> The detection is a machine-learning verdict (Wacatac.B!ml). The library is an emulation runtime: it maps a large guest memory space, runs game threads, reads controller input and sleeps between frames, which is ordinary behaviour for an emulator. The full source code is public. The SDK's developers have also been notified: https://github.com/rexglue/rexglue-sdk/issues/485. Please review and remove the detection.
