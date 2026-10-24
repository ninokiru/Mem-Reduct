Mem Reduct

Description:
Lightweight real-time memory management application to monitor
and clean system memory on your computer.

The program used undocumented internal system features (Native API) to clear system cache (system working set, working set, standby page lists, modified page lists) with variable result ~10-50%. The application targets Windows 10 from version 21H1 and Windows 11.

System requirements:
- Windows 10 version 21H1 (build 19043) or later, including Windows 11 (32-bit and 64-bit)
- An SSE2-capable CPU

The portable archive keeps a 32-bit build in "32" and a 64-bit build in "64". On a 64-bit
Windows run "64\memreduct.exe": the 32-bit build refuses to start under WoW64 unless it is
given the -nowow64 argument. The installer selects the right build automatically.

To activate portable mode, create "memreduct.ini" in application folder, or move it from "%APPDATA%\Lumina\Mem Reduct".

This fork: https://github.com/ninokiru/Mem-Reduct
Maintainer: Lumina
Original author: https://github.com/henrypp

Original work (c) 2011-2026 Henry++
