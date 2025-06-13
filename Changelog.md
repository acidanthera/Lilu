Lilu Changelog
==============
#### v1.7.1
- Allow loading on macOS 26 without `-lilubetaall`, thanks @AlfCraft07

#### v1.7.0
- Added Arrow Lake CPU definitions
- Improved AMD IGPU detection via device-id, thanks @Zormeister

#### v1.6.9
- Fixed loading on macOS 10.10 and older due to a MacKernelSDK regression
- Added AMD IGPU detection via device-id, thanks @Zormeister

#### v1.6.8
- Allow loading on macOS 15 without `-lilubetaall`

#### v1.6.7
- Added Broadwell-EP CPU model
- Added Raptor Lake C0 stepping model

#### v1.6.6
- Fixed macOS 13+ installer detection regression in 1.6.5
- Allow loading on macOS 14 without `-lilubetaall`

#### v1.6.5
- Fixed macOS 13+ recovery and installer detection

#### v1.6.4
- Added AMD IGPU detection

#### v1.6.3
- Added Raptor Lake CPU definitions

#### v1.6.2
- Fixed KC segment name, which also fixed kernel panic on macOS 13 b3
- Disable EFI64 runtime APIs when `-legacy` is used on 32-bit kernels

#### v1.6.1
- Allow loading on macOS 13 without `-lilubetaall`
- Added Ventura dyld shared cache pathing
- Changed SKL default ig-platform-id to KBL on macOS 13+
- Added patch with masking support

#### v1.6.0
- Dropped internal shared patcher instance grabbing API

#### v1.5.9
- Fixed memory corruption when mixing cs_validate_range/page mid/long routes (thx @Goshin)
- Enforced all routes to be slotted after one slotted route
- Refactored all internal routes to use new RouteRequest API
- Deprecated routeFunction APIs as they are dangerous to use for multiple routes

#### v1.5.8
- Fixed kernel panic on macOS 10.15 and earlier introduced in 1.5.7
- Added Alder Lake CPU model support
- Added shared patcher instance grabbing API

#### v1.5.7
- Added address slot support for all 64-bit macOS version

#### v1.5.6
- Added the circular buffer API.
- Added convenient helpers to check a value (available as of C++17).
- Added the `OSObjectWrapper` API to wrap a non-`OSObject` value.

#### v1.5.5
- Added a variant of `KernelPatcher::findAndReplace` that requires both `find` and `replace` buffers to have the same length.
- Added support for macOS 10.4 and newer

#### v1.5.4
- Allow loading on macOS 12 without `-lilubetaall` (With adapted for macOS 12 plug-ins)
- Added guarding for address slot usage to avoid potential kernel routing overflow
- Allow using medium size function routing in the kernel
- Added medium size function routing for `Long` mode as they are functionally equivalent
- Added `matchSharedCachePath` API to support dyld cache matching on macOS 12
- Added `_kmod` hooking for kext listening to unify kext patcher logic
- Added zlib decompression API
- Fixed kernel patcher support on 64-bit 10.6
- Added new GPU switching API

#### v1.5.3
- Fixed kernel patcher support on 10.7

#### v1.5.2
- Fixed AZAL recognition as GPU audio on certain AMD platforms (thx to wkpark)
- Added external GPU disabling API with device and kernel selection via properties
- Added identifiers for Rocket Lake and Tiger Lake CPUs
- Added API to disable builtin GPU (IGPU)
- Reduced hardware presence bruteforce to a more sensible value

#### v1.5.1
- Added `lilu_os_memmem` and `lilu_os_memchr` APIs
- Added `getSharedCachePath` API to obtain current cache path
- Added `LIKELY`/`UNLIKELY` macros

#### v1.5.0
- Fixed Apple HDEF detection made by NVIDIA
- Fixed race-condition in select kext detection during patching (thx to lvs1974)

#### v1.4.9
- Added the PCI GMCH Graphics Control register definition. (by 0xFireWolf)
- Added a new API to solve multiple symbols in one shot conveniently. (by 0xFireWolf)
- Added a new `RouteRequest` constructor to work with function pointers without additional type castings. (by 0xFireWolf)

#### v1.4.8
- Added MacKernelSDK with Xcode 12 compatibility
- Removed `kern_atomic.hpp` due to MacKernelSDK implementation
- Acidanthera MacKernelSDK is now required for all plugins
- Fixed Lilu loading on macOS 10.6 (not all APIs will be functional)
- Fixed plugin debug log not working with Lilu disabled

#### v1.4.7
- Added more platform headers for plugin compilation
- Fixed symbol chainloading regression in 1.4.6

#### v1.4.6
- Added preliminary definitions for 11.0 support
- Temporarily disabled user patcher for 11.0
- Added `external-audio` property to ignore PCI audio cards
- Added in-memory symbol solving for 11.0
- Fixed accidentally solving stabs instead of normal symbols
- Added device publishing API to monitor device startup
- Added DeviceInfo caching for improved performance
- Added implicit slotted (medium) patches in KC mode to reduce patch size

#### v1.4.5
- Fixed newer CPU generation detection
- Added failsafe versions of CML framebuffers

#### v1.4.4
- Added new CFL connector-less framebuffers: 0x9BC80003, 0x9BC50003, 0x9BC40003
- Fixed KDK support disrespecting file suffixes

#### v1.4.3
- Improved modern CPUID detection
- Added BaseDeviceInfo API with improved performance
- Deprecated CPUInfo::getGeneration, WIOKit::getComputerModel(), WIOKit::getComputerInfo()

#### v1.4.2
- Fixed IMEI device detection on some platforms
- Added CometLake CPU model support (thx @stormbirds)
- Added getFatOffset MachO API

#### v1.4.1
- Made applyLookupPatch support kernel patches by passsing null kext
- Export hde64 interface
- Added evector deleter without copying for improved performance
- Allow C strings as module prefix argument to the logging functions

#### v1.4.0
- Fixed mishandling user patches process list after processKernel API call
- Fixed extra I/O in user patcher even when no patches were needed
- Added support for per-process (LocalOnly) userspace patches

#### v1.3.9
- Added QEMU/KVM vendor compatibility to device detection logic

#### v1.3.8
- Compile Xcode 11 OSObject stubs into plugins to allow mixing compilers
- Unified release archive names
- Added multirouting support to routeFunction API enabling functions to have multiple proxies
- Added explicit routing type to routeFunction APIs
- Made Lilu use long function routes to ease third-part multirouting

#### v1.3.7
- Allow loading on 10.15 without `-lilubetaall`
- Add support for Xcode 11 analysis tools
- Add workaround to 10.15 SDK Dispatch method (use old Xcode when possible)

#### v1.3.6
- Lilu now uses OpenCore NVRAM variable GUIDs
- Add support for `0x3E980003` frame id for CFL refresh

#### v1.3.5
- Fixed analog audio device detection on certain laptops with Insyde firmware

#### v1.3.4
- Added implicit `eraseCoverageInstPrefix` to `routeMultiple`
- Fixed user patcher kernel panic when running process via `posix_spawn` without exec
- Fixed user patcher codesign issues on recent 10.14 versions with SIP
- Changed `kern_start` and `kern_stop` to contain product prefix to avoid collisions

#### v1.3.3
- Added support for modern AMD device scanning by @AlGreyy
- Added support for VMware device scanning

#### v1.3.2
- Extended supported firmware vendor list

#### v1.3.1
- Lowered version compatibility to 1.2.0 to let plugins load

#### v1.3.0
- Fixed a rare kernel panic on user patch failure
- Removed unimplemented `genPlatformKey` API

#### v1.2.9
- Added `kern_atomic.hpp` header to support atomic types with old Clang
- Added ThreadLocal APIs
- Added `KernelPatcher::eraseCoverageInstPrefix` API
- Fixed race condition during bootstrap (thx @Download-Fritz)
- Fixed potential race condition during user patching

#### v1.2.8
- Fixed CPU generation detection for Coffee Lake-U
- Fixed PEGP detection with 3D Controller `class-code`
- Fixed userspace patcher compatibility with macOS Mojave
- Allow manually specified reservation in `evector`
- Improved version information printing in DEBUG builds

#### v1.2.7
- Added support for detecting optimus switch-off
- Added Sanitize target with ubsan support (thx to NetBSD)
- Added disk log dump in DEBUG builds via `liludump=N` boot-arg (requires plugin rebuild)
- Fixed multiple Mach-O parsing issues
- Fixed support of PCI devices without compatible property
- Fixed PCI `class-code` masking not detecting HDEF devices

#### v1.2.6
- Added Cannon Lake and Ice Lake definitions
- Added missing typed getOSData APIs
- Added `-liluuseroff` boot-arg to disable user patcher (for e.g. shared cache manipulation)
- Added `lilucpu=N` boot-arg to assume CPU generation
- Added CPU topology detection APIs
- Fixed routeMultiple kernel panic and log report
- Switched to Apple lzvn implementation

#### v1.2.5
- Added new DeviceInfo API
- Added checkKernelArgument API
- Added enforced LiluAPI interfaces
- Added KextInfo::switchOff API
- Added cpuid API
- Allowed for onKextLoad to accept no callback
- Removed GPU detection code from CPUInfo API
- Enabled by default on 10.14

#### v1.2.4
- Internalize new APIs from 1.2.3
- Added new EFI runtime API with custom variable extensions
- Added new RTC storage API
- Added centralised entitlement hooking API
- Added lilu_os_qsort export (the supported interface is Apple-private)
- Added `liludelay=1000` boot argument to insert a 1s delay after each print
- Added new symbol routing API with simplified interface
- Fixed a kernel panic in userspace patching code on 10.14b1

#### v1.2.3
- Added CPU information API for cpu families and generations
- Added IGPU information API for framebuffers and stuff
- Added WIOKit::renameDevice API for device renaming with compatible fixing
- Added KernelPatcher::routeVirtual API for virtual function swapping
- Added PCI register and address manipulation API
- Added basic process modification API
- Added plugin IOService access
- Added address-printing macros
- Added address validation API
- Added strict kext UUID validation to workaround broken kextcache
- Added version info reporting to IORegistry for Lilu and plugins
- Fixed several inline function definitions
- Fixed crash when loading user patches with no binary patches
- Reduced long patch length in function routing API

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
