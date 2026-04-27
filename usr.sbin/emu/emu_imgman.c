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
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION;
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu.h"

extern int g_verbose;

#define EMU_IMAGE_CACHE_DIR	"/var/cache/emu/images"
#define EMU_IMAGE_DEFAULT_URL	"https://download.freebsd.org/snapshots/images"

/*
 * Base image management for emulation framework.
 * Supports downloading, caching, and verifying base disk images.
 */

/*
 * Get the base cache directory for images.
 * Returns a static buffer with the cache path.
 */
static const char *
emu_image_cache_dir(void)
{
	const char *cache_dir;

	cache_dir = getenv("EMU_IMAGE_CACHE");
	if (cache_dir == NULL)
		cache_dir = EMU_IMAGE_CACHE_DIR;

	return (cache_dir);
}

/*
 * Build the cache path for a specific image.
 * Format: <cache_dir>/<arch>/<image_name>.qcow2
 */
static void
emu_image_cache_path(char *path, size_t path_len, const char *arch,
    const char *image_name)
{

	snprintf(path, path_len, "%s/%s/%s.qcow2", emu_image_cache_dir(),
	    arch, image_name);
}

/*
 * Check if an image exists in the cache.
 * Returns 0 if found, ENOENT if not found.
 */
int
emu_image_cached(const char *arch, const char *image_name)
{
	char cache_path[PATH_MAX];
	struct stat sb;

	emu_image_cache_path(cache_path, sizeof(cache_path), arch, image_name);

	if (stat(cache_path, &sb) != 0)
		return (ENOENT);

	if (!S_ISREG(sb.st_mode))
		return (ENOENT);

	return (0);
}

/*
 * Download a base image from a remote URL.
 * Uses fetch(1) or curl(1) if available.
 * Returns 0 on success, error code on failure.
 */
int
emu_image_download(const char *url, const char *arch, const char *image_name,
    const char *checksum)
{
	char cache_path[PATH_MAX];
	char cache_dir_path[PATH_MAX];
	char cmd[PATH_MAX * 2];
	const char *fetch_cmd;
	int ret;

	emu_image_cache_path(cache_path, sizeof(cache_path), arch, image_name);

	/* Create cache directory if needed */
	snprintf(cache_dir_path, sizeof(cache_dir_path), "%s/%s",
	    emu_image_cache_dir(), arch);
	if (mkdirp(cache_dir_path, 0755) != 0) {
		warn("Failed to create cache directory");
		return (errno);
	}

	if (g_verbose)
		printf("Downloading image from %s to %s\n", url, cache_path);

	/* Try fetch(1) first (FreeBSD native) */
	fetch_cmd = "fetch -o '%s' '%s' 2>/dev/null";
	snprintf(cmd, sizeof(cmd), fetch_cmd, cache_path, url);
	ret = system(cmd);
	if (ret != 0) {
		/* Try curl(1) as fallback */
		fetch_cmd = "curl -L -o '%s' '%s' 2>/dev/null";
		snprintf(cmd, sizeof(cmd), fetch_cmd, cache_path, url);
		ret = system(cmd);
		if (ret != 0) {
			warnx("Failed to download image (fetch and curl both failed)");
			return (EIO);
		}
	}

	/* Verify checksum if provided */
	if (checksum != NULL) {
		if (emu_image_verify(cache_path, checksum) != 0) {
			warnx("Image verification failed - checksum mismatch");
			unlink(cache_path);
			return (EACCES);
		}
	}

	if (g_verbose)
		printf("Image downloaded and verified successfully\n");

	return (0);
}

/*
 * Verify an image against a SHA256 checksum.
 * Checksum format: "<sha256hash>  <filename>" or just "<sha256hash>"
 * Returns 0 on success, error code on failure.
 */
int
emu_image_verify(const char *image_path, const char *expected_checksum)
{
	char cmd[PATH_MAX * 2];
	char output[256];
	FILE *fp;
	char *actual_hash;
	size_t len;

	if (expected_checksum == NULL)
		return (0);

	/* Calculate SHA256 of the image */
	snprintf(cmd, sizeof(cmd), "sha256 -q '%s' 2>/dev/null", image_path);
	fp = popen(cmd, "r");
	if (fp == NULL) {
		warn("Failed to calculate checksum");
		return (EIO);
	}

	if (fgets(output, sizeof(output), fp) == NULL) {
		pclose(fp);
		warnx("Failed to read checksum output");
		return (EIO);
	}
	pclose(fp);

	/* Trim newline */
	len = strlen(output);
	if (len > 0 && output[len - 1] == '\n')
		output[len - 1] = '\0';

	actual_hash = output;

	/* Compare checksums */
	if (strcmp(actual_hash, expected_checksum) != 0) {
		if (g_verbose) {
			printf("Expected: %s\n", expected_checksum);
			printf("Actual:   %s\n", actual_hash);
		}
		return (EACCES);
	}

	return (0);
}

/*
 * Install a cached image to an instance directory.
 * Creates a copy of the cached image in the instance's directory.
 * Returns 0 on success, error code on failure.
 */
int
emu_image_install(const char *arch, const char *image_name,
    const char *instance_dir)
{
	char cache_path[PATH_MAX];
	char instance_path[PATH_MAX];
	char cmd[PATH_MAX * 2];
	struct stat sb;
	int ret;

	emu_image_cache_path(cache_path, sizeof(cache_path), arch, image_name);

	/* Check if image is cached */
	if (stat(cache_path, &sb) != 0) {
		warnx("Image not found in cache: %s", cache_path);
		return (ENOENT);
	}

	/* Build instance image path */
	snprintf(instance_path, sizeof(instance_path), "%s/disk.qcow2",
	    instance_dir);

	if (g_verbose)
		printf("Installing image from %s to %s\n", cache_path,
		    instance_path);

	/* Copy the image (use cp for efficiency with sparse files) */
	snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", cache_path, instance_path);
	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to copy image to instance directory");
		return (EIO);
	}

	/* Set appropriate permissions */
	if (chmod(instance_path, 0644) != 0) {
		warn("Failed to set image permissions");
		return (errno);
	}

	return (0);
}

/*
 * Lookup and prepare a base image for an instance.
 * Checks cache first, downloads if needed, verifies checksum.
 * Returns 0 on success, error code on failure.
 */
int
emu_image_prepare(const char *arch, const char *image_name,
    const char *checksum, const char *instance_dir)
{
	char url[PATH_MAX];
	int ret;

	if (g_verbose)
		printf("Preparing base image: %s (%s)\n", image_name, arch);

	/* Check if already cached */
	ret = emu_image_cached(arch, image_name);
	if (ret == 0) {
		if (g_verbose)
			printf("Image found in cache\n");
	} else {
		/* Build download URL */
		snprintf(url, sizeof(url), "%s/%s/%s.qcow2",
		    EMU_IMAGE_DEFAULT_URL, arch, image_name);

		if (g_verbose)
			printf("Downloading from %s\n", url);

		ret = emu_image_download(url, arch, image_name, checksum);
		if (ret != 0) {
			warnx("Failed to download base image");
			return (ret);
		}
	}

	/* Install to instance directory */
	ret = emu_image_install(arch, image_name, instance_dir);
	if (ret != 0) {
		warnx("Failed to install base image");
		return (ret);
	}

	return (0);
}

/*
 * List available cached images.
 * Outputs one image path per line.
 * Returns 0 on success, error code on failure.
 */
int
emu_image_list(void)
{
	char cmd[PATH_MAX];
	int ret;

	/* Use find to list all cached images */
	snprintf(cmd, sizeof(cmd),
	    "find '%s' -name '*.qcow2' -type f 2>/dev/null | sort",
	    emu_image_cache_dir());

	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to list cached images");
		return (EIO);
	}

	return (0);
}

/*
 * Remove a cached image.
 * Returns 0 on success, error code on failure.
 */
int
emu_image_remove(const char *arch, const char *image_name)
{
	char cache_path[PATH_MAX];

	emu_image_cache_path(cache_path, sizeof(cache_path), arch, image_name);

	if (unlink(cache_path) != 0) {
		warn("Failed to remove cached image");
		return (errno);
	}

	if (g_verbose)
		printf("Removed cached image: %s\n", cache_path);

	return (0);
}

/*
 * Clean up old or unused cached images.
 * Removes images older than max_age_days.
 * Returns number of images removed, or -1 on error.
 */
int
emu_image_cleanup(int max_age_days)
{
	char cmd[PATH_MAX];
	int ret;

	if (max_age_days <= 0)
		max_age_days = 30; /* Default: 30 days */

	snprintf(cmd, sizeof(cmd),
	    "find '%s' -name '*.qcow2' -type f -mtime +%d -delete 2>/dev/null",
	    emu_image_cache_dir(), max_age_days);

	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to clean up old images");
		return (-1);
	}

	return (0);
}
