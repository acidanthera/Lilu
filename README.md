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
- [Onyx The Black Cat](https://github.com/gdbinit/onyx-the-black-cat) by [fG!](https://reverse.put.as) for the base of the kernel patcher  
- [capstone](https://github.com/aquynh/capstone) by [Nguyen Anh Quynh](https://github.com/aquynh) for the disassembler module  
- [Pike R. Alpha](https://github.com/Piker-Alpha) for [lzvn](https://github.com/Piker-Alpha/LZVN) decompression  
- [07151129](https://github.com/07151129) for some code parts and suggestions  
- [vit9696](https://github.com/vit9696) for writing the software and maintaining it

#### Installation
You should install this kext along with the plugin kexts depending on it.  
The prebuilt binaries are available on [releases](https://github.com/vit9696/Lilu/releases) page.  
Several existing plugins possibly with code samples are available on [known plugins](https://github.com/vit9696/Lilu/blob/master/KnownPlugins.md) page.

#### Configuration
Add `-liludbg` to enable debug printing (available in DEBUG binaries).  
Add `-liluoff` to disable Lilu.  
Add `-liluslow` to enable legacy user patcher.  
Add `-lilulowmem` to disable kernel unpack (disables Lilu in recovery mode).  
Add `-lilubeta` to enable Lilu on unsupported os versions.

#### Contribution
For the contributors with programming skills the headers are filled with AppleDOC comments.  
Earlier code changes could be tracked in [AppleALC](https://github.com/vit9696/AppleALC) project.   
Writing and supporting code is fun but it takes time. Please provide most descriptive bugreports or pull requests.

