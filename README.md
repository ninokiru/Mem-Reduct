<h1 align="center">Mem Reduct</h1>

<p align="center">
	<a href="https://github.com/ninokiru/Mem-Reduct/releases"><img src="https://img.shields.io/github/v/release/ninokiru/Mem-Reduct?style=flat-square&include_prereleases&label=version" /></a>
	<a href="https://github.com/ninokiru/Mem-Reduct/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow-status/ninokiru/Mem-Reduct/build.yml?style=flat-square&label=build" /></a>
	<a href="https://github.com/ninokiru/Mem-Reduct/releases"><img src="https://img.shields.io/github/downloads/ninokiru/Mem-Reduct/total.svg?style=flat-square" /></a>
	<a href="https://github.com/ninokiru/Mem-Reduct/issues"><img src="https://img.shields.io/github/issues-raw/ninokiru/Mem-Reduct.svg?style=flat-square&label=issues" /></a>
	<a href="https://github.com/ninokiru/Mem-Reduct/graphs/contributors"><img src="https://img.shields.io/github/contributors/ninokiru/Mem-Reduct?style=flat-square" /></a>
	<a href="https://github.com/ninokiru/Mem-Reduct/blob/main/LICENSE"><img src="https://img.shields.io/github/license/ninokiru/Mem-Reduct?style=flat-square" /></a>
</p>

-------

<p align="center">
	<a href="https://github.com/ninokiru/Mem-Reduct">ninokiru/Mem-Reduct</a> - maintained fork of <a href="https://github.com/henrypp/memreduct">henrypp/memreduct</a> (by Henry++), rebuilt as version 0.0.1 with fixes and its own update channel.
</p>

<p align="center">
	<img src="/images/memreduct.png?cachefix" />
</p>
### Description:
Lightweight real-time memory management application to monitor
and clean system memory on your computer.

The program used undocumented internal system features (Native API) to clear system cache (system working set, working set, standby page lists, modified page lists) with variable result ~10-50%. The application targets Windows 10 from version 21H1 and Windows 11.

You can download either the installer or portable version. For correct working you are require administrator rights.

```
To activate portable mode, create "memreduct.ini" in application folder, or move it from "%APPDATA%\Lumina\Mem Reduct".
```

### Command line:
```
memreduct.exe -clean       // clear default memory regions
memreduct.exe -clean:full  // clear all memory regions
```

### System requirements:
- Windows 10 version 21H1 (build 19043) or later, including Windows 11
- 64-bit (x64) or ARM64
- An SSE2-capable CPU

### Support:
This fork is maintained by Lumina and has no donation links of its own. If you want to support the original author, use [henrypp/memreduct#donate](https://github.com/henrypp/memreduct#donate).

### Build & signature:
Binaries for this fork are built by [GitHub Actions](https://github.com/ninokiru/Mem-Reduct/actions/workflows/build.yml) and published to [Releases](https://github.com/ninokiru/Mem-Reduct/releases). Every release carries exactly two assets: the installer (`memreduct-<version>-setup.exe`) and the portable archive (`memreduct-<version>-bin.zip`). They are **not** GPG-signed with the upstream key below, so do not expect `memreduct.exe.sig` next to them.

### Automatic updates:
The application reads its release channel from [`VERSION`](VERSION) on branch `main`. A record looks like `name=version|installer-url|sha256;` - the third field is **required**: the installer is hashed and must match before anything is executed with administrator rights, and a record without it is refused as an invalid manifest. The build workflow prints the ready-to-paste line in its job summary, so after publishing a release copy that line into `VERSION`.

Both the manifest and the installer are fetched through the application's own WinHTTP code: HTTPS only, certificate chain validation always on (no insecure retry fallback), the manifest answer is limited to 32 KB and the installer download to 32 MB (checked both in the announced size and in the bytes actually written). The installer is written to a temporary file whose name is random, then sized, checked and hashed through that one handle, so nothing can be swapped into it while it is downloaded and verified. The handle is closed before the shell starts the file, which leaves a short window after verification; closing it completely would need an ACL the installer can still read through, and the payload stays in a temporary directory whose name is randomly generated, so only a process already running as this user - and able to see that name - could use it.

Upstream signature reference (applies to `henrypp` releases only):

- Public key: [pubkey.asc](https://raw.githubusercontent.com/henrypp/builder/master/pubkey.asc) ([pgpkeys.eu](https://pgpkeys.eu/pks/lookup?op=index&fingerprint=on&search=0x5635B5FD))
- Key ID: 0x5635B5FD
- Fingerprint: D985 2361 1524 AB29 BE73 30AC 2881 20A7 5635 B5FD
---
- This project: [github.com/ninokiru/Mem-Reduct](https://github.com/ninokiru/Mem-Reduct)
- Original author: [github.com/henrypp](https://github.com/henrypp)
---
Original work (c) 2011-2026 Henry++, licensed under GPL-3.0.
