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

The program used undocumented internal system features (Native API) to clear system cache (system working set, working set, standby page lists, modified page lists) with variable result ~10-50%. Application it is compatible with <s>Windows XP SP3</s> Windows 7 SP1 and higher operating systems.

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
- Windows 7, 8, 8.1, 10, 11 (64-bit/ARM64)
- An SSE2-capable CPU
- <s>KB2533623</s> [KB3063858](https://www.microsoft.com/en-us/download/details.aspx?id=47442) update for Windows 7 was required

### Donate (upstream author):
- [Bitcoin](https://www.blockchain.com/btc/address/1LrRTXPsvHcQWCNZotA9RcwjsGcRghG96c) (BTC)
- [Ethereum](https://www.blockchain.com/explorer/addresses/eth/0xe2C84A62eb2a4EF154b19bec0c1c106734B95960) (ETH)
- [Yandex Money](https://yoomoney.ru/to/4100115776040583) (RUB)
- [Paypal](https://paypal.me/henrypp) (USD)

### Build & signature:
Binaries for this fork are built by [GitHub Actions](https://github.com/ninokiru/Mem-Reduct/actions/workflows/build.yml) and published to [Releases](https://github.com/ninokiru/Mem-Reduct/releases). They are **not** GPG-signed with the upstream key below, so do not expect `memreduct.exe.sig` next to them; each release carries a `.sha256` file instead.

Upstream signature reference (applies to `henrypp` releases only):

- Public key: [pubkey.asc](https://raw.githubusercontent.com/henrypp/builder/master/pubkey.asc) ([pgpkeys.eu](https://pgpkeys.eu/pks/lookup?op=index&fingerprint=on&search=0x5635B5FD))
- Key ID: 0x5635B5FD
- Fingerprint: D985 2361 1524 AB29 BE73 30AC 2881 20A7 5635 B5FD
---
- This project: [github.com/ninokiru/Mem-Reduct](https://github.com/ninokiru/Mem-Reduct)
- Original author: [github.com/henrypp](https://github.com/henrypp)
---
Original work (c) 2011-2026 Henry++, licensed under GPL-3.0.
