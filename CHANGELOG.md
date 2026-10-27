v0.0.1 (22 October 2026)

\- first build of the fork, based on the upstream Mem Reduct project by Henry++

\- signed as Lumina, own update channel at ninokiru/Mem-Reduct

\- the tray context menu and the drop-down of the clean button now run their commands (every entry was inert because the selected menu identifier was discarded)

\- the "Website" entry and the About dialog point at ninokiru/Mem-Reduct; the About dialog is built by the fork and keeps only the version, the copyright of the original author and the license footer

\- a refused update manifest and an installer that fails its sha256 check are now reported as verification errors instead of "cannot connect to the update server", and are written to the debug log even when the check ran unattended

\- the build workflow validates `VERSION` on every run: a malformed record or an installer url outside this repository fails the build, a record without the sha256 field warns

\- the update manifest is now read exactly as it is published: the sha256 is the last field of the record (the previous code asked for a fourth separator, so every correct manifest was refused and the updater could never install anything)

\- the installer download is capped, both by the size the server announces and by the bytes actually written, so a hostile answer cannot fill the disk before the hash rejects it

\- the auto-clean value boxes now follow the state of their checkbox instead of the checkbox being merely enabled: an unchecked option no longer leaves its threshold or interval editable

\- the spin arrows of the auto-clean options are now switched off together with their edit box, so a disabled option cannot have its stored value changed from the keyboard or the mouse

\- a failed update request is logged with the address that failed instead of an empty description

\- `memreduct -clean` started without elevation now returns success when the administrative copy was launched, instead of reporting an application initialization failure

\- a cleanup in which at least one memory region failed is now shown with an error notification rather than the ordinary one

\- fixed access violation in the update checker (the application crashed shortly after start when no proxy was configured)

\- fixed installer download always failing on the executable signature check

\- the downloaded installer is now executed only when its sha256 matches the VERSION manifest

\- the sha256 field of the VERSION manifest is now mandatory: a record without it is refused before anything is downloaded

\- the update channel is fetched with the application's own WinHTTP code (https only, certificate chain validation always on, no insecure retry, manifest answer capped at 32 KB) because the shared network helper silently retries a failed certificate check with the unknown-CA and wrong-usage flags ignored

\- the installer is downloaded into a temporary file with a random name, then sized, checked and hashed through that single handle, so the payload cannot be swapped between writing and verifying

\- errors reported by the update worker no longer attach their dialog to a main window that was already destroyed

\- the cleaning summary message is only printed through a language pack string when that string is a well formed "%s" template, so an edited translation cannot crash it

\- failing to acquire SeProfileSingleProcessPrivilege is written to the debug log instead of being ignored

\- purging a single memory region from the tray menu now asks for confirmation, like a full clean does

\- the tray badge no longer creates a zero-height font when the percentage text is squeezed into a small icon

\- fixed standby list rows showing the untranslated region name instead of the formatted one

\- removed dead Windows 7 / 8 / 8.1 version checks and their (win8.1+) labels

\- manifest now declares Windows 10 21H1 and newer, tested up to 10.0.26200

\- localization reduced to English and Vietnamese

\- releases now carry two files only: setup .exe and portable .zip

\- the main window is rebuilt as a fixed 400 px layout: one large percentage, the used and free figures, a row per cached region with a thin bar and a split clean button; the list view, its tooltip and the menu bar are gone

\- the clean button is split in two: the label cleans, the caret opens the menu of the individual regions

\- the window keeps its size in pixels at any dpi and follows a dpi change without a restart

\- the light and dark palettes follow the system theme; the guest / administrator chip and the footer note are localized

\- the "..." button and a right click open the same menu, and the language submenu is rebuilt on every open


\- tray icon is now rendered with the system user interface font, ClearType and a colour that adapts to the warning and danger backgrounds

\- tray icon badge scales the digits down so "100%" still fits inside the small icon

\- application icon now contains 16 to 96 pixel sizes instead of a single stretched one

\- fixed two crashes when the update server answered with a winhttp error code

\- dark theme is now applied to the settings window when it is already open

\- the interface now follows the windows light and dark theme switch without restarting the application

\- the tray tooltip no longer resends an unchanged icon on every timer tick

\- automatic cleanup ignores the "Disable" entry of the usage threshold submenu

\- changing a setting no longer triggers an immediate automatic cleanup

\- automatic cleanup no longer purges the standby and modified lists unless "Allow standby list cleanup" is enabled

\- "-clean" command line switch now returns a real exit code and reports missing administrator rights

\- the cleaning interval submenu and the memory region list are fully translated into Vietnamese

\- removed the duplicated "(Language)" suffix from the language menu and settings title

\- the settings tray menu caption of the "Advanced" page no longer shows the "General configuration" title

\- the "Allow standby lists cleanup" option now wraps over two lines instead of being cut off, in both languages

\- the memory list, the tray tooltip and the tray icon are now only rewritten when their values actually changed, which removes the per-second allocations and full list repaint of a long running session

\- the tray tooltip text is rebuilt only when a displayed value changed, and the group rows no longer re-format identical numbers

\- Vietnamese interface re-translated: 43 strings corrected (menu captions, colour rows, click actions, thresholds, update download wording, the freeze warning), including the mistranslations "Help" -> "Khac", "Website" -> "Ma nguon", "Show/Hide" -> "Bang dieu khien", "Text color" -> "Mau ty so" and "Single click" -> "Chuot trai"

\- the "Give thanks!" entry and the upstream funding link are gone from the help menu, the tray menu and the About dialog

\- releases now ship a 32-bit (x86) and a 64-bit (x64) build for Windows 10 and Windows 11 and no longer build arm64: the installer installs the payload that matches the running Windows, and the portable archive keeps both builds in the "32" and "64" folders

\- all dialogs now ask for Segoe UI 9 directly instead of the logical font "Ms Shell Dlg", which Windows resolves to Microsoft Sans Serif: the main window and the five settings pages grow one step together with their texts, and the stored window position is still honoured because the window has no resize border

\- the manifest declares tested support up to Windows 11 build 26300 (25H2) instead of 26200
\- the automatic update check now repeats on its configured period while the application stays in the tray: before, a check only ever ran at startup, so a six hour period never fired again

\- the tray context menu no longer leaks its menu handle when the loaded popup has no submenu

\- the installer now refuses every Windows older than Windows 10 instead of only refusing Windows 7 and older, which matches the documented system requirements

\- the build workflow now reads the PE headers of both binaries (machine type, GUI subsystem, version resource) instead of running the executable on the runner

\- the README explains building from source, including why `git clone --recursive` cannot work: the submodule paths point above the repository
\- publishing a tag is now the whole release: the release job hashes the installer it uploaded and commits the finished `name=version|url|sha256;` record to `VERSION` on the channel branch, so the updater works without a manual paste

\- that commit refuses to rewind the channel: if `VERSION` already serves a newer version the job fails instead of overwriting it, and a blocked commit (branch protection) is reported as an error

\- clicking Clean, a tray region entry or the hotkey while the application runs without administrator rights now cleans in the elevated copy: the request is stored before the restart, so the second click is gone

\- the figures in the new memory card are refreshed every second instead of only when the percentage changes, so the used and free amounts cannot go stale
