Lilu Changelog
==============

#### v1.2.3
- Added CPU information API for cpu families and generations
- Added IGPU information API for framebuffers and stuff
- Added WIOKit::renameDevice API for device renaming with compatible fixing
- Added KernelPatcher::routeVirtual API for virtual function swapping
- Added PCI register and address manipulation API
- Added basic process modification API
- Added plugin IOService access
- Fixed several inline function definitions
- Fixed crash when loading user patches with no binary patches

#### v1.2.2
- Acknowledged macOS Install Data and com.apple.recovery.boot prelinkedkernel paths (thx Piker-Alpha)
- Fixed ignoring `kcsuffix=<suffix>` for kexts and less common names
- Added extra logging for backtrace macros to ensure that they are not skipped
- Fixed compilation issues with clang not supporting 2 args for deprecated attribute

#### v1.2.1
- Fixed a rare kernel panic when running Lilu with `-liludbg`
- Added a workaround for 10.13.2 beta issues
- Fixed compilation with Xcode 8.2
- Added prelink usage detection to avoid confusing different kernels
- Disabled prelink usage for kext address solving by default since it caused many issues

#### v1.2.0
- Added more handy reporting macros
- Enabled Lilu in safe mode by default with all plugins required to declare supported environments
- Added lzss compression API
- Added crypto and nvram API
- Added support for solving kext symbols from kextcache
- Added memfunc wrappers (e.g. lilu_os_memcpy) to avoid undefined builtins from 10.13 SDK
- Added `-liludbgall` boot argument (to be on par with `-lilubetaall`)
- Added unexact process path matching
- Changed compression API logic to support preallocated buffers
- Changed memory allocation logic in certain APIs
- Changed kernel protection API to accept a lock for cpu preemption control
- Changed KextInfo structure to handle disabled and fsonly kexts
- Changed logging API to enforce more proper style
- Disabled advanced disassembly APIs by default (create an issue if you need them)
- Fixed a memory issue in WIOKit::getComputerInfo introduced in 1.1.7
- Fixed several assertions triggering in 10.13 development kernel
- Fixed Xcode 9 compiled binary compatibility with older OS
- Fixed FAT_CIGAM and FAT_MAGIC parsing issues
- Fixed a number of potential memory issues in mach parsing code
- Fixed debug and development kextcache loading issues
- Fixed shutdown issues in `-lilulowmem` mode
- Fixed seldom boot slowdown when disabling the plugins via boot arguments

#### v1.1.7
- Merged advanced disassembly API (thx Pb and others)
- Added HDE disassembler for quick instruction decoding (by Vyacheslav Patkov)
- Updated capstone to 3.0.5 rc3
- Fixed load API lock type preventing dynamic memory allocation (thx Pb)
- Added setInterrupts API
- Added an option to define custom plugin entry points
- Added const reference evector API
- Added FAT_CIGAM Mach-O support
- Added WIOKit::getComputerInfo API and improved some other WIOKit APIs
- Added support of storing larger than pointer types in evector
- Added `-lilubetaall` boot argument to skip version checking for all plugins

#### v1.1.6
- Ignored disabled kexts earlier for speed reasons
- Added High Sierra to the list of compatible OS
- Added arrsize API
- Made patch count warning only show in debug mode
- Made kinfo not found logging only show in debug mode
- Added routeBlock API for opcode-based routing
- Centralised user and kernel patcher start time
- Added c-compliant kern_os_cfree implementation
- Added a workaround for page fault kernel panics
- Added a workaround for xnu printf limitations

#### v1.1.5
- Increased executable memory buffer to page size
- Added auth-root-dmg support High Sierra installer detection (thx Piker-Alpha)
- Added -liluforce to force enable Lilu in safe mode and recovery
- Added preliminary Xcode 9 compatibility
- Added support for unloadable kexts
- Merged official capstone patches up to c508224

#### v1.1.4
- Slightly improved userspace patcher speed for 10.12
- Added missing dyld_shared_cache detection with a fallback
- Defined High Sierra kernel version

#### v1.1.3
- Reduced binary size by modding capstone
- Fixed LiluAPI::onProcLoad return code
- Added MachInfo::setRunningAddresses for userspace symbol solving
- Added getKernelMinorVersion for symmetry
- Added kernel write protection and interrupt state validation

#### v1.1.1
- Changed loading policy to ignore kexts that are not permitted to load
- Increased executable memory buffer from 256 to 1024 bytes
- Allowed different plugins load the same kexts

#### v1.1.0
- Added support for patching different sections/segments
- Added file writing API by lvs1974
- Added strrchr API
- Changed requestAccess to include API version to workaround kext loading issues
- Updated capstone to 3.0.5 rc2
- Improved 32-bit userspace patcher
- Enforced `-liluslow` in installer and recovery

#### v1.0
- Initial release
