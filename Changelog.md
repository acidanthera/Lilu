Lilu Changelog
==============

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
