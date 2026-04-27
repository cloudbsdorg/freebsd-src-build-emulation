/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _EMU_IMAGE_H_
#define	_EMU_IMAGE_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Emulation Framework - Base Image Management
 *
 * This module provides base image download, caching, and validation
 * for emulated instances.
 *
 * Security considerations:
 * - Images verified with SHA-256 checksums
 * - Download from trusted sources only
 * - Cache integrity validation
 * - No execution of downloaded content
 */

/* Maximum image name length */
#define	EMU_IMAGE_NAME_MAX	64

/* Maximum image path length */
#define	EMU_IMAGE_PATH_MAX	PATH_MAX

/* Maximum hash length (SHA-256 hex) */
#define	EMU_IMAGE_HASH_LEN	64

/* Image types */
enum emu_image_type {
	EMU_IMAGE_UNKNOWN = 0,
	EMU_IMAGE_KERNEL,		/* Kernel image */
	EMU_IMAGE_ROOTFS,		/* Root filesystem */
	EMU_IMAGE_BOOT,			/* Boot loader */
	EMU_IMAGE_FIRMWARE,		/* Firmware blob */
	EMU_IMAGE_DTB			/* Device tree blob */
};

/* Image descriptor */
struct emu_image {
	char		name[EMU_IMAGE_NAME_MAX];	/* Image name */
	char		path[EMU_IMAGE_PATH_MAX];	/* Local cache path */
	char		url[EMU_IMAGE_PATH_MAX];	/* Download URL */
	char		hash[EMU_IMAGE_HASH_LEN];	/* SHA-256 hash */
	enum emu_image_type	type;		/* Image type */
	int		arch;				/* Architecture */
	size_t		size;				/* Size in bytes */
	bool		cached;				/* Image is cached */
	bool		verified;			/* Image is verified */
	time_t		download_time;			/* Download timestamp */
};

/* Image cache configuration */
struct emu_image_cache {
	char		cache_dir[EMU_IMAGE_PATH_MAX];	/* Cache directory */
	struct emu_image	images[64];	/* Cached images */
	int		num_images;			/* Number of cached images */
	size_t		max_size;			/* Max cache size */
	size_t		current_size;			/* Current cache size */
	bool		initialized;			/* Cache initialized */
};

/*
 * Image cache management
 */

/* Initialize image cache */
int emu_image_cache_init(struct emu_image_cache *cache, const char *cache_dir);

/* Cleanup image cache */
int emu_image_cache_cleanup(struct emu_image_cache *cache);

/* Get cache statistics */
int emu_image_cache_stats(struct emu_image_cache *cache, size_t *used,
    size_t *max, int *count);

/*
 * Image operations
 */

/* Download image to cache */
int emu_image_download(struct emu_image_cache *cache, const char *url,
    const char *expected_hash);

/* Find image in cache by name */
struct emu_image *emu_image_find(struct emu_image_cache *cache,
    const char *name);

/* Find image in cache by hash */
struct emu_image *emu_image_find_by_hash(struct emu_image_cache *cache,
    const char *hash);

/* Verify image integrity */
int emu_image_verify(struct emu_image *image);

/* Remove image from cache */
int emu_image_remove(struct emu_image_cache *cache, const char *name);

/* List all cached images */
int emu_image_list(struct emu_image_cache *cache, struct emu_image **list,
    int max_items);

/* Get image info */
int emu_image_info(struct emu_image *image, char *buf, size_t len);

/*
 * Utility functions
 */

/* Calculate SHA-256 hash of file */
int emu_image_calculate_hash(const char *path, char *hash, size_t hash_len);

/* Verify file hash */
int emu_image_verify_hash(const char *path, const char *expected_hash);

/* Get default cache directory */
const char *emu_image_default_cache_dir(void);

/* Convert image type to string */
const char *emu_image_type_str(enum emu_image_type type);

/* Convert architecture to string */
const char *emu_image_arch_str(int arch);

#endif /* !_EMU_IMAGE_H_ */
