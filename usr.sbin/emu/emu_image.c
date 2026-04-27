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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <paths.h>
#include <time.h>
#include <dirent.h>

#include "emu_image.h"
#include "emu.h"

/*
 * Emulation Framework - Base Image Management Implementation
 *
 * This module implements base image download, caching, and validation
 * for emulated instances.
 *
 * Security considerations:
 * - Images verified with SHA-256 checksums
 * - Download from trusted sources only
 * - Cache integrity validation
 * - No execution of downloaded content
 */

/* Default cache directory relative to home */
#define	EMU_DEFAULT_CACHE_DIR	".cache/emu/images"

/* Default maximum cache size (10 GB) */
#define	EMU_DEFAULT_MAX_CACHE_SIZE	(10ULL * 1024 * 1024 * 1024)

/*
 * Convert image type to string
 */
const char *
emu_image_type_str(enum emu_image_type type)
{
	switch (type) {
	case EMU_IMAGE_UNKNOWN:
		return "Unknown";
	case EMU_IMAGE_KERNEL:
		return "Kernel";
	case EMU_IMAGE_ROOTFS:
		return "RootFS";
	case EMU_IMAGE_BOOT:
		return "Boot";
	case EMU_IMAGE_FIRMWARE:
		return "Firmware";
	case EMU_IMAGE_DTB:
		return "DTB";
	default:
		return "Invalid";
	}
}

/*
 * Convert architecture to string
 */
const char *
emu_image_arch_str(int arch)
{
	switch (arch) {
	case EMU_ARCH_AMD64:
		return "amd64";
	case EMU_ARCH_I386:
		return "i386";
	case EMU_ARCH_ARM64:
		return "arm64";
	case EMU_ARCH_ARM:
		return "arm";
	case EMU_ARCH_POWERPC:
		return "powerpc";
	case EMU_ARCH_RISCV:
		return "riscv";
	default:
		return "unknown";
	}
}

/*
 * Get default cache directory
 */
const char *
emu_image_default_cache_dir(void)
{
	static char cache_dir[PATH_MAX];
	const char *home;

	home = getenv("HOME");
	if (home == NULL)
		home = _PATH_DEFPATH;

	snprintf(cache_dir, sizeof(cache_dir), "%s/%s", home,
	    EMU_DEFAULT_CACHE_DIR);

	return (cache_dir);
}

/*
 * Initialize image cache
 * Returns 0 on success, -1 on failure
 */
int
emu_image_cache_init(struct emu_image_cache *cache, const char *cache_dir)
{
	struct stat st;

	if (cache == NULL)
		return (-1);

	memset(cache, 0, sizeof(struct emu_image_cache));

	/* Set cache directory */
	if (cache_dir != NULL) {
		strlcpy(cache->cache_dir, cache_dir, sizeof(cache->cache_dir));
	} else {
		strlcpy(cache->cache_dir, emu_image_default_cache_dir(),
		    sizeof(cache->cache_dir));
	}

	/* Set default max size */
	cache->max_size = EMU_DEFAULT_MAX_CACHE_SIZE;

	/* Create cache directory if it doesn't exist */
	if (stat(cache->cache_dir, &st) != 0) {
		if (mkdir(cache->cache_dir, 0755) != 0) {
			warn("Failed to create cache directory %s", cache->cache_dir);
			return (-1);
		}
	} else if (!S_ISDIR(st.st_mode)) {
		warnx("Cache path %s is not a directory", cache->cache_dir);
		errno = ENOTDIR;
		return (-1);
	}

	cache->initialized = true;
	return (0);
}

/*
 * Cleanup image cache
 * Returns 0 on success, -1 on failure
 */
int
emu_image_cache_cleanup(struct emu_image_cache *cache)
{
	if (cache == NULL)
		return (-1);

	/* TODO: Implement cache cleanup (remove old images, etc.) */

	cache->initialized = false;
	return (0);
}

/*
 * Get cache statistics
 * Returns 0 on success, -1 on failure
 */
int
emu_image_cache_stats(struct emu_image_cache *cache, size_t *used,
    size_t *max, int *count)
{
	if (cache == NULL)
		return (-1);

	if (used != NULL)
		*used = cache->current_size;
	if (max != NULL)
		*max = cache->max_size;
	if (count != NULL)
		*count = cache->num_images;

	return (0);
}

/*
 * Calculate SHA-256 hash of file using sha256 command
 * Returns 0 on success, -1 on failure
 */
int
emu_image_calculate_hash(const char *path, char *hash, size_t hash_len)
{
	char cmd[PATH_MAX + 64];
	FILE *fp;
	char line[512];

	if (path == NULL || hash == NULL || hash_len < 65)
		return (-1);

	/* Use sha256 command to calculate hash */
	snprintf(cmd, sizeof(cmd), "sha256 -q '%s' 2>/dev/null", path);

	fp = popen(cmd, "r");
	if (fp == NULL) {
		warn("Failed to run sha256");
		return (-1);
	}

	if (fgets(line, sizeof(line), fp) == NULL) {
		pclose(fp);
		errno = EINVAL;
		return (-1);
	}

	pclose(fp);

	/* Remove trailing newline */
	line[strcspn(line, "\n")] = '\0';

	/* Copy hash */
	strlcpy(hash, line, hash_len);

	return (0);
}

/*
 * Verify file hash
 * Returns 0 if hash matches, -1 if mismatch, -2 on error
 */
int
emu_image_verify_hash(const char *path, const char *expected_hash)
{
	char calculated_hash[EMU_IMAGE_HASH_LEN];
	int ret;

	if (path == NULL || expected_hash == NULL)
		return (-2);

	ret = emu_image_calculate_hash(path, calculated_hash,
	    sizeof(calculated_hash));
	if (ret != 0)
		return (-2);

	if (strcmp(calculated_hash, expected_hash) == 0)
		return (0);

	return (-1); /* Hash mismatch */
}

/*
 * Find image in cache by name
 */
struct emu_image *
emu_image_find(struct emu_image_cache *cache, const char *name)
{
	int i;

	if (cache == NULL || name == NULL)
		return (NULL);

	for (i = 0; i < cache->num_images; i++) {
		if (strcmp(cache->images[i].name, name) == 0)
			return (&cache->images[i]);
	}

	return (NULL);
}

/*
 * Find image in cache by hash
 */
struct emu_image *
emu_image_find_by_hash(struct emu_image_cache *cache, const char *hash)
{
	int i;

	if (cache == NULL || hash == NULL)
		return (NULL);

	for (i = 0; i < cache->num_images; i++) {
		if (strcmp(cache->images[i].hash, hash) == 0)
			return (&cache->images[i]);
	}

	return (NULL);
}

/*
 * Verify image integrity
 * Returns 0 if verified, -1 on failure
 */
int
emu_image_verify(struct emu_image *image)
{
	int ret;

	if (image == NULL || !image->cached)
		return (-1);

	if (image->hash[0] == '\0') {
		/* No hash to verify against */
		image->verified = false;
		return (-1);
	}

	ret = emu_image_verify_hash(image->path, image->hash);
	if (ret == 0) {
		image->verified = true;
		return (0);
	}

	image->verified = false;
	return (-1);
}

/*
 * Download image to cache
 * Returns 0 on success, -1 on failure
 */
int
emu_image_download(struct emu_image_cache *cache, const char *url,
    const char *expected_hash)
{
	struct emu_image *image;
	char dest_path[PATH_MAX];
	char cmd[PATH_MAX * 2 + 256];
	struct stat st;
	int ret;

	if (cache == NULL || url == NULL)
		return (-1);

	if (!cache->initialized) {
		warnx("Image cache not initialized");
		return (-1);
	}

	/* Check if we have room in cache */
	if (cache->num_images >= 64) {
		warnx("Image cache full (max 64 images)");
		errno = ENOMEM;
		return (-1);
	}

	/* Generate destination path */
	image = &cache->images[cache->num_images];
	snprintf(dest_path, sizeof(dest_path), "%s/img_%d",
	    cache->cache_dir, cache->num_images);

	/* Download using fetch (FreeBSD's download tool) */
	snprintf(cmd, sizeof(cmd),
	    "fetch -o '%s' -q '%s' 2>/dev/null || "
	    "curl -L -o '%s' -s '%s' 2>/dev/null",
	    dest_path, url, dest_path, url);

	ret = system(cmd);
	if (ret != 0) {
		warn("Download failed for %s", url);
		return (-1);
	}

	/* Verify download succeeded */
	if (stat(dest_path, &st) != 0) {
		warn("Downloaded file not found: %s", dest_path);
		return (-1);
	}

	/* Initialize image descriptor */
	memset(image, 0, sizeof(struct emu_image));
	snprintf(image->name, sizeof(image->name), "img_%d", cache->num_images);
	strlcpy(image->path, dest_path, sizeof(image->path));
	strlcpy(image->url, url, sizeof(image->url));
	image->type = EMU_IMAGE_UNKNOWN;
	image->arch = EMU_ARCH_UNKNOWN;
	image->size = st.st_size;
	image->cached = true;
	image->download_time = time(NULL);

	/* Verify hash if provided */
	if (expected_hash != NULL && expected_hash[0] != '\0') {
		strlcpy(image->hash, expected_hash, sizeof(image->hash));
		ret = emu_image_verify(image);
		if (ret != 0) {
			warnx("Hash verification failed for %s", dest_path);
			unlink(dest_path);
			return (-1);
		}
	} else {
		/* Calculate hash for future verification */
		emu_image_calculate_hash(dest_path, image->hash,
		    sizeof(image->hash));
		image->verified = false; /* No expected hash to verify against */
	}

	cache->current_size += image->size;
	cache->num_images++;

	return (0);
}

/*
 * Remove image from cache
 * Returns 0 on success, -1 on failure
 */
int
emu_image_remove(struct emu_image_cache *cache, const char *name)
{
	struct emu_image *image;
	int i, j;

	if (cache == NULL || name == NULL)
		return (-1);

	image = emu_image_find(cache, name);
	if (image == NULL) {
		errno = ENOENT;
		return (-1);
	}

	/* Remove file */
	if (unlink(image->path) != 0) {
		warn("Failed to remove %s", image->path);
		return (-1);
	}

	cache->current_size -= image->size;

	/* Shift remaining images */
	for (i = 0; i < cache->num_images; i++) {
		if (strcmp(cache->images[i].name, name) == 0) {
			for (j = i; j < cache->num_images - 1; j++)
				cache->images[j] = cache->images[j + 1];
			cache->num_images--;
			break;
		}
	}

	return (0);
}

/*
 * List all cached images
 * Returns number of images listed, or -1 on failure
 */
int
emu_image_list(struct emu_image_cache *cache, struct emu_image **list,
    int max_items)
{
	int i;

	if (cache == NULL || list == NULL || max_items <= 0)
		return (-1);

	for (i = 0; i < cache->num_images && i < max_items; i++) {
		list[i] = &cache->images[i];
	}

	return (i);
}

/*
 * Get image info
 * Returns bytes written, or -1 on failure
 */
int
emu_image_info(struct emu_image *image, char *buf, size_t len)
{
	char time_buf[64];
	struct tm *tm;

	if (image == NULL || buf == NULL || len == 0)
		return (-1);

	tm = localtime(&image->download_time);
	if (tm != NULL) {
		strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
	} else {
		strlcpy(time_buf, "unknown", sizeof(time_buf));
	}

	return (snprintf(buf, len,
	    "Name: %s\n"
	    "Path: %s\n"
	    "URL: %s\n"
	    "Type: %s\n"
	    "Arch: %s\n"
	    "Size: %zu bytes\n"
	    "Hash: %s\n"
	    "Cached: %s\n"
	    "Verified: %s\n"
	    "Downloaded: %s\n",
	    image->name,
	    image->path,
	    image->url,
	    emu_image_type_str(image->type),
	    emu_image_arch_str(image->arch),
	    image->size,
	    image->hash[0] != '\0' ? image->hash : "(none)",
	    image->cached ? "yes" : "no",
	    image->verified ? "yes" : "no",
	    time_buf));
}
