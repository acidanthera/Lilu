//
//  kern_file.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_util.hpp>

#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>

uint8_t *FileIO::readFileToBuffer(const char *path, size_t &size) {
	size = 0;
	vnode_t vnode = NULLVP;
	vfs_context_t ctxt = vfs_context_create(nullptr);
	if (!ctxt)
		return nullptr;
	uint8_t *buf = nullptr;

	errno_t err = vnode_lookup(path, 0, &vnode, ctxt);
	if(!err) {
		auto fileSize = readFileSize(vnode, ctxt);
		if (fileSize > 0 && fileSize < Buffer::BufferMax) {
			buf = Buffer::create<uint8_t>(fileSize + 1);
			if (buf) {
				if (readFileData(buf, 0, fileSize, vnode, ctxt)) {
					SYSLOG("file", "failed to read %s file of %lu size", path, fileSize);
					Buffer::deleter(buf);
					buf = nullptr;
				} else {
					// Guarantee null termination
					buf[fileSize] = 0x00;
					size = fileSize;
				}
			} else {
				SYSLOG("file", "failed to allocate memory for reading %s file of %lu size", path, fileSize);
			}
		} else {
			SYSLOG("file", "invalid size %lu for %s", fileSize, path);
		}
		vnode_put(vnode);
	} else {
		SYSLOG("file", "failed to find %s", path);
	}

	vfs_context_rele(ctxt);

	return buf;
}


int FileIO::readFileData(void *buffer, off_t off, size_t size, vnode_t vnode, vfs_context_t ctxt) {
	return performFileIO(buffer, off, size, vnode, ctxt, false);
}

size_t FileIO::readFileSize(vnode_t vnode, vfs_context_t ctxt) {
	// Taken from XNU vnode_size
	vnode_attr va;
	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_data_size);
	if (vnode_getattr(vnode, &va, ctxt) || !VATTR_IS_SUPPORTED(&va, va_data_size))
		return 0;
#ifdef __i386__
	if (va.va_data_size > SIZE_MAX)
		return 0;
#endif
	return static_cast<size_t>(va.va_data_size);
}

int FileIO::writeBufferToFile(const char *path, void *buffer, size_t size, int fmode, int cmode) {
	vnode_t vnode = NULLVP;
	vfs_context_t ctxt = vfs_context_create(nullptr);
	if (!ctxt)
		return ENOMEM;

	errno_t err = vnode_open(path, fmode, cmode, VNODE_LOOKUP_NOFOLLOW, &vnode, ctxt);
	if (!err) {
		err = writeFileData(buffer, 0, size, vnode, ctxt);
		if (err)
			SYSLOG("file", "failed to write %s file of %lu size", path, size);
		auto closeErr = vnode_close(vnode, fmode | (err ? 0 : FWASWRITTEN), ctxt);
		if (closeErr)
			SYSLOG("file", "vnode_close(%s) failed with error %d!", path, closeErr);
		if (!err)
			err = closeErr;
	} else {
		SYSLOG("file", "failed to create file %s with error %d", path, err);
	}

	vfs_context_rele(ctxt);

	return err;
}

int FileIO::writeFileData(void *buffer, off_t off, size_t size, vnode_t vnode, vfs_context_t ctxt) {
	return performFileIO(buffer, off, size, vnode, ctxt, true);
}

int FileIO::performFileIO(void *buffer, off_t off, size_t size, vnode_t vnode, vfs_context_t ctxt, bool write) {
	uio_t uio = uio_create(1, off, UIO_SYSSPACE, write ? UIO_WRITE : UIO_READ);
	if (!uio) {
		SYSLOG("file", "uio_create returned null!");
		return EINVAL;
	}

	int error = uio_addiov(uio, CAST_USER_ADDR_T(buffer), size);
	if (error) {
		SYSLOG("file", "uio_addiov returned error %d!", error);
	} else {
		error = write ? VNOP_WRITE(vnode, uio, 0, ctxt) : VNOP_READ(vnode, uio, 0, ctxt);
		if (error) {
			SYSLOG("file", "%s failed %d!", write ? "VNOP_WRITE" : "VNOP_READ", error);
		} else if (uio_resid(uio)) {
			SYSLOG("file", "uio_resid returned non-null!");
			error = EINVAL;
		}
	}

	uio_free(uio);
	return error;
}
