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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emu.h"

extern int g_verbose;
extern int g_quiet;

#define EMU_IMAGE_DEFAULT_ARCH	"amd64"

/*
 * emu image - Manage base disk images for emulation framework
 *
 * Usage: emu image <command> [options]
 * Commands:
 *   list                  List cached images
 *   download <name>       Download an image
 *   remove <name>         Remove a cached image
 *   cleanup               Remove old images
 */

static void
image_usage(void)
{
	fprintf(stderr, "Usage: emu image <command> [options]\n");
	fprintf(stderr, "\nCommands:\n");
	fprintf(stderr, "  list                  List cached images\n");
	fprintf(stderr, "  download <name>       Download an image\n");
	fprintf(stderr, "  remove <name>         Remove a cached image\n");
	fprintf(stderr, "  cleanup               Remove old images\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  --arch=ARCH           Architecture (default: %s)\n",
	    EMU_IMAGE_DEFAULT_ARCH);
	fprintf(stderr, "  --url=URL             Download URL\n");
	fprintf(stderr, "  --checksum=SHA256     SHA256 checksum for verification\n");
	fprintf(stderr, "  --age=DAYS            Max age in days for cleanup (default: 30)\n");
	fprintf(stderr, "  -v, --verbose         Verbose output\n");
	fprintf(stderr, "  -h, --help            Show this help message\n");
	exit(EX_USAGE);
}

int
emu_cmd_image(int argc, char *argv[])
{
	const char *command = NULL;
	const char *arch = EMU_IMAGE_DEFAULT_ARCH;
	const char *url = NULL;
	const char *checksum = NULL;
	const char *image_name = NULL;
	int max_age = 30;
	int ch;
	int ret;

	if (argc < 2)
		image_usage();

	command = argv[1];
	argc -= 2;
	argv += 2;

	while ((ch = getopt(argc, argv, "a:c:v:h")) != -1) {
		switch (ch) {
		case 'a':
			arch = optarg;
			break;
		case 'c':
			checksum = optarg;
			break;
		case 'v':
			g_verbose = 1;
			break;
		case 'h':
			image_usage();
			break;
		default:
			image_usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (strcmp(command, "list") == 0) {
		if (argc > 0)
			image_usage();

		ret = emu_image_list();
		if (ret != 0) {
			warnx("Failed to list images");
			return (EX_SOFTWARE);
		}
		return (0);
	}

	if (strcmp(command, "download") == 0) {
		if (argc < 1) {
			warnx("Image name required");
			image_usage();
		}
		image_name = argv[0];

		/* Check if already cached */
		ret = emu_image_cached(arch, image_name);
		if (ret == 0) {
			if (!g_quiet)
				printf("Image '%s' already cached for %s\n",
				    image_name, arch);
			return (0);
		}

		/* Download the image */
		if (url == NULL) {
			warnx("Download URL required (use --url option)");
			return (EX_USAGE);
		}

		ret = emu_image_download(url, arch, image_name, checksum);
		if (ret != 0) {
			warnx("Failed to download image");
			return (EX_IOERR);
		}

		if (!g_quiet)
			printf("Image '%s' downloaded successfully for %s\n",
			    image_name, arch);
		return (0);
	}

	if (strcmp(command, "remove") == 0) {
		if (argc < 1) {
			warnx("Image name required");
			image_usage();
		}
		image_name = argv[0];

		ret = emu_image_remove(arch, image_name);
		if (ret != 0) {
			warnx("Failed to remove image");
			return (EX_IOERR);
		}

		if (!g_quiet)
			printf("Image '%s' removed for %s\n", image_name, arch);
		return (0);
	}

	if (strcmp(command, "cleanup") == 0) {
		ret = emu_image_cleanup(max_age);
		if (ret < 0) {
			warnx("Failed to cleanup old images");
			return (EX_SOFTWARE);
		}

		if (!g_quiet)
			printf("Cleanup completed (removed images older than %d days)\n",
			    max_age);
		return (0);
	}

	warnx("Unknown image command: %s", command);
	image_usage();
	return (EX_USAGE);
}
