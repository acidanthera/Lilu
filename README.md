Lilu
====

An open source kernel extension bringing a platform for arbitrary kext, library, and program patching throughout the system for macOS.

#### Features
- Generic kext patcher
- Generic process patcher (currently 64-bit only)
- Generic framework/library patcher (currently 64-bit only)
- Provides a unified plugin API

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [Brad Conte](https://github.com/B-Con) for [SHA-256 implementation](https://github.com/B-Con/crypto-algorithms)
- [fG!](https://github.com/gdbinit) for [Onyx The Black Cat](https://github.com/gdbinit/onyx-the-black-cat) used as a base of the kernel patcher
- [Nguyen Anh Quynh](https://github.com/aquynh) for [capstone](https://github.com/aquynh/capstone) disassembler module
- [Pike R. Alpha](https://github.com/Piker-Alpha) for [lzvn](https://github.com/Piker-Alpha/LZVN) decompression
- [Ralph Hempel](https://github.com/rhempel) for [umm_malloc](https://github.com/rhempel/umm_malloc) static pool allocator
- Vyacheslav Patkov for [hde64](https://github.com/mumble-voip/minhook/tree/7d80cff1de5c87b404e7ac451757bfa77e5e82da/src/hde) simple disassembler module
- [07151129](https://github.com/07151129) for some code parts and suggestions
- [vit9696](https://github.com/vit9696) for writing the software and maintaining it

#### Installation
You should install this kext along with the plugin kexts depending on it.  
The prebuilt binaries are available on [releases](https://github.com/vit9696/Lilu/releases) page.  
Several existing plugins possibly with code samples are available on [known plugins](https://github.com/vit9696/Lilu/blob/master/KnownPlugins.md) page.
To compile a plugin copy the debug version of Lilu.kext into its directory.

#### Configuration
Add `-liludbg` to enable debug printing (available in DEBUG binaries).  
Add `-liluoff` to disable Lilu.  
Add `-liluslow` to enable legacy user patcher.  
Add `-lilulowmem` to disable kernel unpack (disables Lilu in recovery mode).  
Add `-lilubeta` to enable Lilu on unsupported os versions (10.13 and below are enabled by default).  
Add `-lilubetaall` to enable Lilu and all loaded plugins on unsupported os versions (use _very_ carefully)  
Add `-liluforce` to enable Lilu regardless of the os, installer, or recovery.

#### Discussion
[InsanelyMac topic](http://www.insanelymac.com/forum/topic/321371-lilu-—-kext-and-process-patcher/) in English  
[AppleLife topic](https://applelife.ru/threads/lilu-patcher-kekstov-i-processov.1964133/) in Russian

#### Contribution
For the contributors with programming skills the headers are filled with AppleDOC comments.  
Earlier code changes could be tracked in [AppleALC](https://github.com/vit9696/AppleALC) project.   
Writing and supporting code is fun but it takes time. Please provide most descriptive bugreports or pull requests.
