//
//  kern_mach.hpp
//  Lilu
//
//  Certain parts of code are the subject of
//   copyright © 2011, 2012, 2013, 2014 fG!, reverser@put.as - http://reverse.put.as
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_mach_hpp
#define kern_mach_hpp

#include <Headers/kern_config.hpp>
#include <Headers/kern_util.hpp>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <mach-o/loader.h>
#include <mach/vm_param.h>
#include <libkern/c++/OSDictionary.h>

enum MachType {
	Kext = 0,
	Kernel,
	KextCollection
};

#define LC_FILESET_ENTRY 0x80000035
typedef struct {
  	uint32_t commandType;
  	uint32_t commandSize;
	uint64_t virtualAddress;
	uint64_t fileOffset;
  	uint32_t stringOffset;
  	uint32_t stringAddress32;
} fileset_entry_command;

// Copied from mach-o/fixup-chains.h
// header of the LC_DYLD_CHAINED_FIXUPS payload
struct dyld_chained_fixups_header
{
    uint32_t    fixups_version;    // 0
    uint32_t    starts_offset;     // offset of dyld_chained_starts_in_image in chain_data
    uint32_t    imports_offset;    // offset of imports table in chain_data
    uint32_t    symbols_offset;    // offset of symbol strings in chain_data
    uint32_t    imports_count;     // number of imported symbol names
    uint32_t    imports_format;    // DYLD_CHAINED_IMPORT*
    uint32_t    symbols_format;    // 0 => uncompressed, 1 => zlib compressed
};

// This struct is embedded in LC_DYLD_CHAINED_FIXUPS payload
struct dyld_chained_starts_in_image
{
    uint32_t    seg_count;
    uint32_t    seg_info_offset[1];  // each entry is offset into this struct for that segment
    // followed by pool of dyld_chain_starts_in_segment data
};

// This struct is embedded in dyld_chain_starts_in_image
// and passed down to the kernel for page-in linking
struct dyld_chained_starts_in_segment
{
    uint32_t    size;               // size of this (amount kernel needs to copy)
    uint16_t    page_size;          // 0x1000 or 0x4000
    uint16_t    pointer_format;     // DYLD_CHAINED_PTR_*
    uint64_t    segment_offset;     // offset in memory to start of segment
    uint32_t    max_valid_pointer;  // for 32-bit OS, any value beyond this is not a pointer
    uint16_t    page_count;         // how many pages are in array
    uint16_t    page_start[1];      // each entry is offset in each page of first element in chain
                                    // or DYLD_CHAINED_PTR_START_NONE if no fixups on page
 // uint16_t    chain_starts[1];    // some 32-bit formats may require multiple starts per page.
                                    // for those, if high bit is set in page_starts[], then it
                                    // is index into chain_starts[] which is a list of starts
                                    // the last of which has the high bit set
};

// DYLD_CHAINED_PTR_64_KERNEL_CACHE, DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE
struct dyld_chained_ptr_64_kernel_cache_rebase
{
    uint64_t    target     : 30,   // basePointers[cacheLevel] + target
                cacheLevel :  2,   // what level of cache to bind to (indexes a mach_header array)
                diversity  : 16,
                addrDiv    :  1,
                key        :  2,
                next       : 12,    // 1 or 4-byte stide
                isAuth     :  1;    // 0 -> not authenticated.  1 -> authenticated
};

union ChainedFixupPointerOnDisk {
	uint64_t raw64;
	struct dyld_chained_ptr_64_kernel_cache_rebase fixup64;
};

struct KextInjectionInfo {
	const char *identifier = nullptr; // Optional; fetched automatically from the plist if omitted
	const char *bundlePath;
	const char *infoPlist;
	uint32_t infoPlistSize;
	const char *executablePath; // Used iff executable != nullptr
  	const uint8_t *executable = nullptr; // Optional
	uint32_t executableSize; // Used iff executable != nullptr
};

struct KCPatchInfo {
	uint64_t patchStart;
	uint64_t patchEnd; // Inclusive
	uint8_t *patchWith;
};

class MachInfo {
#if defined(__i386__)
	using mach_header_native = mach_header;
	using segment_command_native = segment_command;
	using nlist_native = struct nlist;

	static constexpr uint8_t SegmentTypeNative {LC_SEGMENT};
	static constexpr uint32_t MachMagicNative {MH_MAGIC};
	static constexpr uint32_t MachCpuTypeNative {CPU_TYPE_I386};

#elif defined(__x86_64__)
	using mach_header_native = mach_header_64;
	using segment_command_native = segment_command_64;
	using nlist_native = struct nlist_64;

	static constexpr uint8_t SegmentTypeNative {LC_SEGMENT_64};
	static constexpr uint32_t MachMagicNative {MH_MAGIC_64};
	static constexpr uint32_t MachCpuTypeNative {CPU_TYPE_X86_64};
#else
#error Unsupported arch.
#endif

	mach_vm_address_t running_text_addr {0}; 		// the address of running __TEXT segment
	mach_vm_address_t disk_text_addr {0};    		// the same address at from a file
	uint64_t          text_size {0};         		// size of the __TEXT segment
	mach_vm_address_t kaslr_slide {0};       		// the kernel aslr slide, computed as the difference between above's addresses
	OSDictionary *prelink_dict {nullptr};    		// read prealinked kext dictionary
	uint8_t *prelink_addr {nullptr};         		// prelink text base address
	mach_vm_address_t prelink_vmaddr {0};    		// prelink text base vm address (for kexts this is their actual slide)
	uint8_t *file_buf {nullptr};             		// read file data
	uint32_t file_buf_size {0};              		// read file data size
	uint32_t file_orig_size {0};                    // original size of the file without the free space
	uint32_t file_buf_free_start {0};        		// start of the free space inside the file (for injecting new prelink info / kexts)
	uint32_t linkedit_increase {0};                 // amount of space to reserve after the original __LINKEDIT
	uint32_t linkedit_offset {0};            		// file offset to __LINKEDIT
	uint32_t linkedit_free_start {0};        		// start of the free space inside the __LINKEDIT segment (for injecting new kexts)
	uint32_t branch_stubs_offset {0};        		// file offset to __BRANCH_STUBS
	uint32_t branch_gots_offset {0};         		// file offset to __BRANCH_GOTS
	OSDictionary *branch_gots_entries {nullptr};  	// entries inside __BRANCH_GOTS
	uint32_t branch_got_entry_count {0};     		// amount of entries inside __BRANCH_GOTS
	uint8_t *sym_buf {nullptr};              		// pointer to buffer (normally __LINKEDIT) containing symbols to solve
	bool sym_buf_ro {false};                 		// sym_buf is read-only (not copy).
	uint64_t sym_fileoff {0};                		// file offset of symbols (normally __LINKEDIT) so we can read
	size_t sym_size {0};
	uint32_t symboltable_fileoff {0};        // file offset to symbol table - used to position inside the __LINKEDIT buffer
	uint32_t symboltable_nr_symbols {0};
	uint32_t stringtable_fileoff {0};        // file offset to string table
	uint32_t stringtable_size {0};
	mach_header_native *running_mh {nullptr};   // pointer to mach-o header of running kernel item
	mach_vm_address_t address_slots {0};     	// pointer after mach-o header to store pointers
	mach_vm_address_t address_slots_end {0}; 	// pointer after mach-o header to store pointers
	off_t fat_offset {0};                    	// additional fat offset
	size_t memory_size {HeaderSize};         	// memory size
	bool kaslr_slide_set {false};            	// kaslr can be null, used for disambiguation
	bool allow_decompress {true};            	// allows mach decompression
	bool prelink_slid {false};               	// assume kaslr-slid kext addresses
	bool is_kc {false};          			 	// Is Kext Collection (11.0+)
	uint64_t self_uuid[2] {};                	// saved uuid of the loaded kext or kernel
	uint32_t kexts_injected {0};             	// amount of kexts injected into the KC so far
	uint32_t cur_kc_kind {0};                   // Kind of the KC
	uint32_t kc_index {0};                   	// Index of the KC (kc_kind2index)
	OSDictionary *kc_symbols {nullptr};      	// Exported symbols from various KCs
	OSArray *imageArr {nullptr};				// KC Prelink infos
	OSArray *kc_patch_info;                     // Ranges of the KC to patch and what to patch them with

	/**
	 *  Kernel slide is aligned by 20 bits
	 */
	static constexpr size_t KASLRAlignment {0x100000};

	/**
	 *  Retrieve LC_UUID command value from a mach header
	 *
	 *  @param header mach header pointer
	 *
	 *  @return UUID or nullptr
	 */
	uint64_t *getUUID(void *header);

	/**
	 *  Retrieve and preserve LC_UUID command value from a mach header
	 *
	 *  @param header mach header pointer
	 *
	 *  @return true on success
	 */
	bool loadUUID(void *header);

	/**
	 *  Enable/disable the Write Protection bit in CR0 register
	 *
	 *  @param enable the desired value
	 *
	 *  @return KERN_SUCCESS if succeeded
	 */
	static kern_return_t setWPBit(bool enable);

	/**
	 *  Retrieve the first pages of a binary at disk into a buffer
	 *  Version that uses KPI VFS functions and a ripped uio_createwithbuffer() from XNU
	 *
	 *  @param buffer     allocated buffer sized no less than HeaderSize
	 *  @param vnode      file node
	 *  @param ctxt       filesystem context
	 *  @param decompress enable decompression
	 *  @param off        fat offset or 0
	 *
	 *  @return KERN_SUCCESS if the read data contains 64-bit mach header
	 */
	kern_return_t readMachHeader(uint8_t *buffer, vnode_t vnode, vfs_context_t ctxt, off_t off=0);

	/**
	 *  Retrieve the whole symbol table (typically contained within the linkedit segment) into target buffer from kernel binary at disk
	 *
	 *  @param vnode file node
	 *  @param ctxt  filesystem context
	 *
	 *  @return KERN_SUCCESS on success
	 */
	kern_return_t readSymbols(vnode_t vnode, vfs_context_t ctxt);

	/**
	 *  Retrieve necessary mach-o header information from the mach header
	 *
	 *  @param header read header sized no less than HeaderSize
	 */
	void processMachHeader(void *header);

	/**
	 *  Load kext info dictionary and addresses if they were not loaded previously
	 */
	void updatePrelinkInfo();

	/**
	 *  Lookup mach image in prelinked image
	 *
	 *  @param identifier  identifier
	 *  @param imageIndex  index of the image inside the prelink info
	 *  @param imageSize   size of the returned buffer
	 *  @param slide       actual slide for symbols (normally kaslr or 0)
	 *  @param missing     set to true on successful prelink parsing when image is not needed
	 *
	 *  @return pointer to const buffer on success or nullptr
	 */
	uint8_t *findImage(const char *identifier, uint32_t &imageIndex, uint32_t &imageSize, mach_vm_address_t &slide, bool &missing);

	MachInfo(MachType machType, const char *id) : machType(machType), objectId(id) {
		DBGLOG("mach", "MachInfo type %d object constructed", machType);
	}
	MachInfo(const MachInfo &) = delete;
	MachInfo &operator =(const MachInfo &) = delete;

	/**
	 *  Resolve mach data in the kernel via prelinked cache
	 *
	 *  @param prelink    prelink information source (i.e. Kernel MachInfo)
	 *
	 *  @return KERN_SUCCESS if loaded
	 */
	kern_return_t initFromPrelinked(MachInfo *prelink);

	/**
	 *  Resolve mach data in the kernel via filesystem access
	 *
	 *  @param paths      filesystem paths for lookup
	 *  @param num        the number of paths passed
	 *
	 *  @return KERN_SUCCESS if loaded
	 */
	kern_return_t initFromFileSystem(const char * const paths[], size_t num);

	/**
	 *  Resolve mach data in the kernel via memory access
	 *
	 *  @return KERN_SUCCESS if loaded
	 */
	kern_return_t initFromMemory();

public:
	/**
	 *  Each header is assumed to fit two pages
	 */
	static constexpr size_t HeaderSize {PAGE_SIZE_64*2};

	/**
	 *  Representation mode
	 */
	EXPORT const MachType machType;

	/**
	 *  Specified file identifier
	 */
	EXPORT const char *objectId {nullptr};

	/**
	 *  MachInfo object generator
	 *
	 *  @param machType type of this MachInfo
	 *  @param id       kinfo identifier (e.g. CFBundleIdentifier)
	 *
	 *  @return MachInfo object or nullptr
	 */
	static MachInfo *create(MachType machType=MachType::Kext, const char *id=nullptr) { return new MachInfo(machType, id); }
	static void deleter(MachInfo *i NONNULL) { delete i; }

	/**
	 *  Resolve mach data in the kernel
	 *
	 *  @param paths      filesystem paths for lookup
	 *  @param num        the number of paths passed
	 *  @param prelink    prelink information source (i.e. Kernel MachInfo)
	 *  @param fsfallback fallback to reading from filesystem if prelink failed
	 *
	 *  @return KERN_SUCCESS if loaded
	 */
	EXPORT kern_return_t init(const char * const paths[], size_t num = 1, MachInfo *prelink=nullptr, bool fsfallback=false);

	/**
	 *  Release the allocated memory, must be called regardless of the init error
	 */
	EXPORT void deinit();

	/**
	 *  Retrieve the mach header and __TEXT addresses for KC mode
	 *
	 *  @param slide load slide if calculating for kexts
	 *
	 *  @return KERN_SUCCESS on success
	 */
	kern_return_t kcGetRunningAddresses(mach_vm_address_t slide);

	/**
	 *  Get address slot if present
	 *
	 *  @return address slot on success
	 *  @return NULL on success
	 */
	mach_vm_address_t getAddressSlot();

	/**
	 *  Retrieve the mach header and __TEXT addresses
	 *
	 *  @param slide load slide if calculating for kexts
	 *  @param size  memory size
	 *  @param force force address recalculation
	 *
	 *  @return KERN_SUCCESS on success
	 */
	EXPORT kern_return_t getRunningAddresses(mach_vm_address_t slide=0, size_t size=0, bool force=false);

	/**
	 *  Set the mach header address
	 *
	 *  @param slide load address
	 *  @param size  memory size
	 *
	 *  @return KERN_SUCCESS on success
	 */
	EXPORT kern_return_t setRunningAddresses(mach_vm_address_t slide=0, size_t size=0);

	/**
	 *  Retrieve running mach positions
	 *
	 *  @param header pointer to header
	 *  @param size   file size
	 */
	EXPORT void getRunningPosition(uint8_t * &header, size_t &size);

	/**
	 *  Solve a mach symbol (running addresses must be calculated)
	 *
	 *  @param symbol symbol to solve
	 *
	 *  @return running symbol address or 0
	 */
	EXPORT mach_vm_address_t solveSymbol(const char *symbol);

	/**
	 *  Find the kernel base address (mach-o header)
	 *
	 *  @return kernel base address or 0
	 */
	EXPORT mach_vm_address_t findKernelBase();

	/**
	 *  Compare the loaded kernel with the current UUID (see loadUUID)
	 *
	 *  @param base  image base, pass 0 to use kernel base
	 *
	 *  @return true if image uuids match
	 */
	EXPORT bool isCurrentBinary(mach_vm_address_t base=0);

	/**
	 *  Enable/disable interrupt handling
	 *  this is similar to ml_set_interrupts_enabled except the return value
	 *
	 *  @param enable the desired value
	 *
	 *  @return true if changed the value and false if it is unchanged
	 */
	EXPORT static bool setInterrupts(bool enable);

	/**
	 *  Enable/disable kernel memory write protection
	 *
	 *  @param enable  the desired value
	 *  @param lock    use spinlock to disable cpu preemption (see KernelPatcher::kernelWriteLock)
	 *
	 *  @return KERN_SUCCESS if succeeded
	 */
	EXPORT static kern_return_t setKernelWriting(bool enable, IOSimpleLock *lock);

	/**
	 *  Find section bounds in a passed binary for provided cpu
	 *
	 *  @param ptr           pointer to a complete mach-o binary
	 *  @param sourceSize    size of the mach-o binary
	 *  @param vmsegment     returned vm segment pointer
	 *  @param vmsection     returned vm section pointer
	 *  @param sectionptr    returned section pointer
	 *  @param sectionSize   returned section size or 0 on failure
	 *  @param segmentCmdPtr pointer to the mach command of the returned segment
	 *  @param sectionCmdPtr pointer to the mach command of the returned section
	 *  @param segmentName   segment name
	 *  @param sectionName   section name
	 *  @param cpu           cpu to look for in case of fat binaries
	 */
	EXPORT static void findSectionBounds(void *ptr, size_t sourceSize, vm_address_t &vmsegment, vm_address_t &vmsection, void *&sectionptr, size_t &sectionSize, void *&segmentCmdPtr, void *&sectionCmdPtr, const char *segmentName="__TEXT", const char *sectionName="__text", cpu_type_t cpu=CPU_TYPE_X86_64);

	/**
	 *  Extract x86_64 binary from a FAT binary
	 *
	 *  @param executable     pointer to the original executable. Returns pointer to the extracted executable
	 *  @param executableSize size of the original executable. Returns size of the extracted executable
	 *
	 *  @return true if the executable is not FAT or if the extraction succeeded
	 */
	EXPORT static bool extractFatBinary(const uint8_t *&executable, uint32_t &executableSize);

	/**
	 *  Request to free file buffer resources (not including linkedit symtable)
	 */
	void freeFileBufferResources();

	/**
	 *  Get fat offset of the initialised image
	 */
	off_t getFatOffset() {
		return fat_offset;
	}

	/**
	 *  Resolve mach data in an image via memory buffer
	 *
	 *  @return KERN_SUCCESS if loaded
	 */
	kern_return_t initFromBuffer(uint8_t *buf, uint32_t bufSize, uint32_t origBufSize);

	/**
	 *  Block a kext from the KC
	 *
	 *  @return KERN_SUCCESS if the kext was found and blocked/excluded
	 */
	kern_return_t blockKextFromKC(const char *identifier, bool exclude);

	/**
	 *  Inject a kext from the KC
	 *
	 *  @return KERN_SUCCESS if the kext was injected
	 */
	kern_return_t injectKextIntoKC(const KextInjectionInfo *injectInfo);

	/**
	 *  Overwrite the prelink info in the file buffer and add header/appendix patch info
	 *
	 *  @return KERN_SUCCESS if nothing went wrong
	 */
	kern_return_t finalizeKCInject();

	/**
	 *  Parse all kexts in the KC and add exported symbols to kcSymbols
	 *
	 *  @return KERN_SUCCESS if all kexts were parsed successfully
 	 */
	kern_return_t extractKextsSymbols();

	/**
	 *  Create and append KC patch info based on file_buf
	 */
	void addKCPatchInfo(uint64_t patchStart, uint64_t patchSize);

	/**
	 *  Get the file buffer of the initialised image
	 */
	uint8_t *getFileBuf() {
		return file_buf;
	}

	/**
	 *  Get the size of the __TEXT segment
	 */
	uint64_t getTextSize() {
		return text_size;
	}

	/**
	 *  Set the KC kind and index
	 *  KCKindPageable -> 1, KCKindAuxiliary -> 3
	 */
	void setKcKindAndIndex(uint32_t kind, uint32_t index) {
		cur_kc_kind = kind;
		kc_index = index;
	}

    /**
	 *  Set the KC symbols
	 */
	void setKcSymbols(OSDictionary *kcSymbols) {
		kc_symbols = kcSymbols;
	}

	/**
	 *  Get the file offset of the symbols
	 */
	uint64_t getSymFileOff() {
		return sym_fileoff;
	}

	/**
	 *  Set the symbol buffer
	 */
	void setSymBuf(uint8_t *symBuf) {
		sym_buf = symBuf;
	}

	/**
	 *  Set the KC patch info
	 */
	void setKcPatchInfo(OSArray *kcPatchInfo) {
		kc_patch_info = kcPatchInfo;
	}

	/**
	 *  Set amount of space to reserve after the original __LINKEDIT
	 */
	void setLinkeditIncrease(uint32_t linkeditIncrease) {
		linkedit_increase = linkeditIncrease;
	}
};

#endif /* kern_mach_hpp */
