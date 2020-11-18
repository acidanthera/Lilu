Lilu
====

[![Build Status](https://travis-ci.com/acidanthera/Lilu.svg?branch=master)](https://travis-ci.com/acidanthera/Lilu) [![Scan Status](https://scan.coverity.com/projects/16137/badge.svg?flat=1)](https://scan.coverity.com/projects/16137)

An open source kernel extension bringing a platform for arbitrary kext, library, and program patching throughout the system for macOS.

#### Features
- Generic kext patcher
- Generic process patcher (64-bit with basic 32-bit functionality)
- Generic framework/library patcher (64-bit with basic 32-bit functionality)
- Provides a unified plugin API

#### Installation
You should install this kext along with the plugin kexts depending on it.  
The prebuilt binaries are available on [releases](https://github.com/vit9696/Lilu/releases) page.  
Several existing plugins possibly with code samples are available on [known plugins](https://github.com/vit9696/Lilu/blob/master/KnownPlugins.md) page.
To compile a plugin copy the debug version of Lilu.kext into its directory.

#### Configuration
- Add `-liludbg` to enable debug printing (available in DEBUG binaries).
- Add `-liludbgall` to enable debug printing in Lilu and all loaded plugins (available in DEBUG binaries).
- Add `-liluoff` to disable Lilu.
- Add `-liluuseroff` to disable Lilu user patcher (for e.g. dyld_shared_cache manipulations).
- Add `-liluslow` to enable legacy user patcher.
- Add `-lilulowmem` to disable kernel unpack (disables Lilu in recovery mode).
- Add `-lilubeta` to enable Lilu on unsupported OS versions (11 and below are enabled by default).
- Add `-lilubetaall` to enable Lilu and all loaded plugins on unsupported os versions (use _very_ carefully).
- Add `-liluforce` to enable Lilu regardless of the mode, OS, installer, or recovery.
- Add `liludelay=1000` to enable 1 second delay after each print for troubleshooting.
- Add `lilucpu=N` to let Lilu and plugins assume Nth CPUInfo::CpuGeneration.
- Add `liludump=N` to let Lilu DEBUG version dump log to `/var/log/Lilu_VERSION_KERN_MAJOR.KERN_MINOR.txt` after N seconds

#### Peculiarities
Most of the plugins cease to function in safe (`-x`) mode.  
By default Lilu itself does not function in single-user (`-s`) mode, unless `-liluforce` is present.

#### Discussion
[InsanelyMac topic](http://www.insanelymac.com/forum/topic/321371-lilu-—-kext-and-process-patcher/) in English  
[AppleLife topic](https://applelife.ru/threads/lilu-patcher-kekstov-i-processov.1964133/) in Russian

#### Contribution
For the contributors with programming skills the headers are filled with AppleDOC comments.  
Earlier code changes could be tracked in [AppleALC](https://github.com/vit9696/AppleALC) project.  
Writing and supporting code is fun but it takes time. Please provide most descriptive bugreports or pull requests.

#### Credits
- [Apple](https://www.apple.com) for macOS  and [lzvn](https://github.com/lzfse/lzfse) decompression
- [Brad Conte](https://github.com/B-Con) for [SHA-256 implementation](https://github.com/B-Con/crypto-algorithms)
- [fG!](https://github.com/gdbinit) for [Onyx The Black Cat](https://github.com/gdbinit/onyx-the-black-cat) used as a base of the kernel patcher
- [Nguyen Anh Quynh](https://github.com/aquynh) for [capstone](https://github.com/aquynh/capstone) disassembler module
- [Pike R. Alpha](https://github.com/Piker-Alpha) for original [lzvn](https://github.com/Piker-Alpha/LZVN) decompression
- [Ralph Hempel](https://github.com/rhempel) for [umm_malloc](https://github.com/rhempel/umm_malloc) static pool allocator
- Vyacheslav Patkov for [hde64](https://github.com/mumble-voip/minhook/tree/7d80cff1de5c87b404e7ac451757bfa77e5e82da/src/hde) simple disassembler module
- [07151129](https://github.com/07151129) for some code parts and suggestions
- [vit9696](https://github.com/vit9696) for writing the software and maintaining it
