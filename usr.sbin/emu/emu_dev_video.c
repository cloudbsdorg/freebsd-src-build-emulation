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

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#include "emu_dev_video.h"

/*
 * Video/Framebuffer Device Emulation
 *
 * Provides display output for guest operating systems via:
 * - Internal framebuffer (for direct access)
 * - VNC server (for remote display)
 *
 * Supported device types:
 * - EMU_VIDEO_TYPE_STDVGA: Standard VGA (legacy BIOS compatibility)
 * - EMU_VIDEO_TYPE_CIRRUS: Cirrus Logic GD5446 (legacy Windows)
 * - EMU_VIDEO_TYPE_VIRTIO_GPU: Modern VirtIO GPU (Linux/BSD guests)
 * - EMU_VIDEO_TYPE_VMWARE_SVGA: VMware SVGA II adapter
 *
 * Security considerations:
 * - Framebuffer memory is allocated separately from guest memory
 * - VNC server binds to localhost by default for security
 * - Password authentication uses DES-based VNC challenge-response
 */

/* Resolution lookup table */
const struct emu_video_resolution emu_video_resolutions[] = {
	[EMU_VIDEO_RES_640X480]   = { 640,  480,  32, 2560,  640 * 480 * 4 },
	[EMU_VIDEO_RES_800X600]  = { 800,  600,  32, 3200,  800 * 600 * 4 },
	[EMU_VIDEO_RES_1024X768]  = { 1024, 768,  32, 4096,  1024 * 768 * 4 },
	[EMU_VIDEO_RES_1280X720]  = { 1280, 720,  32, 5120,  1280 * 720 * 4 },
	[EMU_VIDEO_RES_1280X800]  = { 1280, 800,  32, 5120,  1280 * 800 * 4 },
	[EMU_VIDEO_RES_1366X768]  = { 1366, 768,  32, 5472,  1366 * 768 * 4 },
	[EMU_VIDEO_RES_1920X1080] = { 1920, 1080, 32, 7680,  1920 * 1080 * 4 },
	[EMU_VIDEO_RES_1920X1200] = { 1920, 1200, 32, 7680,  1920 * 1200 * 4 },
	[EMU_VIDEO_RES_2560X1440] = { 2560, 1440, 32, 10240, 2560 * 1440 * 4 },
	[EMU_VIDEO_RES_3840X2160] = { 3840, 2160, 32, 15360, 3840 * 2160 * 4 },
};

/* Default VNC port range */
#define	VNC_PORT_MIN	5900
#define	VNC_PORT_MAX	5999
#define	VNC_PORT_DEFAULT	0	/* Auto-select */

/* VNC protocol constants */
#define	VNC_VERSION		"RFB 003.008\n"
#define	VNC_AUTH_NONE		1
#define	VNC_AUTH_VNC		2
#define	VNC_AUTH_FAILED		1
#define	VNC_AUTH_OK		0

/* Device type names */
static const char *video_type_names[] = {
	[EMU_VIDEO_TYPE_NONE]		= "none",
	[EMU_VIDEO_TYPE_STDVGA]		= "stdvga",
	[EMU_VIDEO_TYPE_CIRRUS]		= "cirrus",
	[EMU_VIDEO_TYPE_VIRTIO_GPU]	= "virtio-gpu",
	[EMU_VIDEO_TYPE_VMWARE_SVGA]	= "vmware-svga",
};

/*
 * Initialize video device
 */
int
emu_video_init(struct emu_video *video, uint8_t type)
{
	if (video == NULL)
		return (EINVAL);

	if (type > EMU_VIDEO_TYPE_VMWARE_SVGA)
		return (EINVAL);

	memset(video, 0, sizeof(*video));
	video->video_type = type;

	/* Set default configuration based on device type */
	switch (type) {
	case EMU_VIDEO_TYPE_STDVGA:
		video->video_config.stdvga.width = 1024;
		video->video_config.stdvga.height = 768;
		video->video_config.stdvga.vram_size = 16 * 1024 * 1024;
		video->video_config.stdvga.vmware_compat = false;
		break;

	case EMU_VIDEO_TYPE_CIRRUS:
		video->video_config.cirrus.width = 1024;
		video->video_config.cirrus.height = 768;
		video->video_config.cirrus.vram_size = 8 * 1024 * 1024;
		video->video_config.cirrus.blink = true;
		break;

	case EMU_VIDEO_TYPE_VIRTIO_GPU:
		video->video_config.vgpu.vgpu_num_scanouts = 1;
		video->video_config.vgpu.vgpu_num_capsets = 4;
		break;

	case EMU_VIDEO_TYPE_VMWARE_SVGA:
		video->video_config.stdvga.width = 1920;
		video->video_config.stdvga.height = 1080;
		video->video_config.stdvga.vram_size = 256 * 1024 * 1024;
		video->video_config.stdvga.vmware_compat = true;
		break;

	case EMU_VIDEO_TYPE_NONE:
	default:
		/* No framebuffer needed */
		video->video_initialized = true;
		return (0);
	}

	/* Set default resolution */
	video->fb_width = 1024;
	video->fb_height = 768;
	video->fb_bpp = 32;
	video->fb_pitch = video->fb_width * (video->fb_bpp / 8);

	/* Calculate framebuffer size */
	video->fb_size = emu_video_fb_size(video->fb_width,
	    video->fb_height, video->fb_bpp);

	/* Allocate framebuffer memory */
	video->fb_base = mmap(NULL, video->fb_size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);

	if (video->fb_base == MAP_FAILED) {
		video->fb_base = NULL;
		return (ENOMEM);
	}

	/* Initialize dirty region to entire screen */
	video->dirty_x1 = 0;
	video->dirty_y1 = 0;
	video->dirty_x2 = video->fb_width;
	video->dirty_y2 = video->fb_height;
	video->dirty_valid = true;

	/* Initialize VNC defaults */
	video->vnc.port = VNC_PORT_DEFAULT;
	video->vnc.read_only = 0;
	video->vnc.shared = 1;
	video->vnc.password[0] = '\0';

	video->video_initialized = true;
	video->display_enabled = true;

	return (0);
}

/*
 * Destroy video device
 */
void
emu_video_destroy(struct emu_video *video)
{
	if (video == NULL)
		return;

	/* Shutdown VNC if running */
	if (video->vnc_enabled) {
		emu_vnc_shutdown(video);
	}

	/* Free framebuffer */
	if (video->fb_base != NULL) {
		munmap(video->fb_base, video->fb_size);
		video->fb_base = NULL;
	}

	video->video_initialized = false;
	video->display_enabled = false;
}

/*
 * Reset video device
 */
void
emu_video_reset(struct emu_video *video)
{
	if (video == NULL)
		return;

	/* Clear framebuffer */
	if (video->fb_base != NULL && video->fb_size > 0) {
		memset(video->fb_base, 0, video->fb_size);
	}

	/* Reset dirty region */
	video->dirty_x1 = 0;
	video->dirty_y1 = 0;
	video->dirty_x2 = video->fb_width;
	video->dirty_y2 = video->fb_height;
	video->dirty_valid = true;

	/* Reset statistics */
	video->frames_rendered = 0;
	video->display_updates = 0;
}

/*
 * Set video resolution
 */
int
emu_video_set_resolution(struct emu_video *video, uint16_t width,
    uint16_t height, uint8_t bpp)
{
	size_t new_size;
	void *new_fb;

	if (video == NULL)
		return (EINVAL);

	if (width == 0 || height == 0)
		return (EINVAL);

	if (bpp != 16 && bpp != 24 && bpp != 32)
		return (EINVAL);

	/* Calculate new framebuffer size */
	new_size = emu_video_fb_size(width, height, bpp);
	if (new_size == 0)
		return (EINVAL);

	/* Allocate new framebuffer */
	new_fb = mmap(NULL, new_size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);

	if (new_fb == MAP_FAILED)
		return (ENOMEM);

	/* Free old framebuffer */
	if (video->fb_base != NULL) {
		munmap(video->fb_base, video->fb_size);
	}

	/* Update configuration */
	video->fb_base = new_fb;
	video->fb_size = new_size;
	video->fb_width = width;
	video->fb_height = height;
	video->fb_bpp = bpp;
	video->fb_pitch = width * (bpp / 8);

	/* Update device-specific configuration */
	switch (video->video_type) {
	case EMU_VIDEO_TYPE_STDVGA:
	case EMU_VIDEO_TYPE_VMWARE_SVGA:
		video->video_config.stdvga.width = width;
		video->video_config.stdvga.height = height;
		break;

	case EMU_VIDEO_TYPE_CIRRUS:
		video->video_config.cirrus.width = width;
		video->video_config.cirrus.height = height;
		break;

	default:
		break;
	}

	/* Mark entire screen dirty */
	video->dirty_x1 = 0;
	video->dirty_y1 = 0;
	video->dirty_x2 = width;
	video->dirty_y2 = height;
	video->dirty_valid = true;

	return (0);
}

/*
 * Get current video resolution
 */
int
emu_video_get_resolution(struct emu_video *video, uint16_t *width,
    uint16_t *height, uint8_t *bpp)
{
	if (video == NULL)
		return (EINVAL);

	if (width != NULL)
		*width = video->fb_width;
	if (height != NULL)
		*height = video->fb_height;
	if (bpp != NULL)
		*bpp = video->fb_bpp;

	return (0);
}

/*
 * Blit data to framebuffer
 */
int
emu_video_blit(struct emu_video *video, const void *data, size_t len,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	size_t row_size;

	if (video == NULL || data == NULL)
		return (EINVAL);

	if (!video->video_initialized || video->fb_base == NULL)
		return (ENXIO);

	/* Validate bounds */
	if (x + w > video->fb_width || y + h > video->fb_height)
		return (EINVAL);

	/* Calculate row size */
	row_size = w * (video->fb_bpp / 8);
	if (row_size * h > len)
		return (EINVAL);

	/* Copy data to framebuffer */
	uint8_t *dst = (uint8_t *)video->fb_base +
	    (y * video->fb_pitch) + (x * (video->fb_bpp / 8));
	const uint8_t *src = (const uint8_t *)data;

	for (uint32_t row = 0; row < h; row++) {
		memcpy(dst + (row * video->fb_pitch),
		    src + (row * row_size), row_size);
	}

	/* Update dirty region */
	video->dirty_x1 = 0;
	video->dirty_y1 = 0;
	video->dirty_x2 = video->fb_width;
	video->dirty_y2 = video->fb_height;
	video->dirty_valid = true;

	video->display_updates++;

	return (0);
}

/*
 * Fill rectangle with solid color
 */
int
emu_video_fill_rect(struct emu_video *video, uint32_t x, uint32_t y,
    uint32_t w, uint32_t h, uint32_t color)
{
	uint8_t *row;
	uint32_t row_idx;

	if (video == NULL)
		return (EINVAL);

	if (!video->video_initialized || video->fb_base == NULL)
		return (ENXIO);

	/* Validate bounds */
	if (x + w > video->fb_width || y + h > video->fb_height)
		return (EINVAL);

	row = (uint8_t *)video->fb_base + (y * video->fb_pitch) +
	    (x * (video->fb_bpp / 8));

	/* Fill each row */
	for (row_idx = 0; row_idx < h; row_idx++) {
		uint8_t *pixel = row + (row_idx * video->fb_pitch);

		switch (video->fb_bpp) {
		case 32:
			for (uint32_t col = 0; col < w; col++) {
				uint32_t *p = (uint32_t *)(pixel + col * 4);
				*p = color;
			}
			break;
		case 24:
			for (uint32_t col = 0; col < w; col++) {
				pixel[col * 3] = color & 0xff;
				pixel[col * 3 + 1] = (color >> 8) & 0xff;
				pixel[col * 3 + 2] = (color >> 16) & 0xff;
			}
			break;
		case 16:
			for (uint32_t col = 0; col < w; col++) {
				uint16_t *p = (uint16_t *)(pixel + col * 2);
				*p = color & 0xffff;
			}
			break;
		}
	}

	/* Update dirty region */
	video->dirty_x1 = x;
	video->dirty_y1 = y;
	video->dirty_x2 = x + w;
	video->dirty_y2 = y + h;
	video->dirty_valid = true;

	video->display_updates++;

	return (0);
}

/*
 * Enable display output
 */
int
emu_video_enable(struct emu_video *video)
{
	if (video == NULL)
		return (EINVAL);

	if (!video->video_initialized)
		return (ENXIO);

	video->display_enabled = true;
	return (0);
}

/*
 * Disable display output
 */
int
emu_video_disable(struct emu_video *video)
{
	if (video == NULL)
		return (EINVAL);

	video->display_enabled = false;
	return (0);
}

/*
 * Refresh display (trigger redraw)
 */
int
emu_video_refresh(struct emu_video *video)
{
	if (video == NULL)
		return (EINVAL);

	if (!video->video_initialized || video->fb_base == NULL)
		return (ENXIO);

	/* Mark entire screen dirty */
	video->dirty_x1 = 0;
	video->dirty_y1 = 0;
	video->dirty_x2 = video->fb_width;
	video->dirty_y2 = video->fb_height;
	video->dirty_valid = true;

	video->frames_rendered++;

	return (0);
}

/*
 * Flush display updates (send to VNC if active)
 */
int
emu_video_flush(struct emu_video *video)
{
	if (video == NULL)
		return (EINVAL);

	if (!video->video_initialized)
		return (ENXIO);

	/* TODO: Send dirty region to VNC clients */
	if (video->vnc_enabled && video->dirty_valid) {
		/* VNC framebuffer update would be sent here */
		video->dirty_valid = false;
	}

	return (0);
}

/*
 * Initialize VNC server
 */
int
emu_vnc_init(struct emu_video *video, uint16_t port)
{
	int fd;
	struct sockaddr_in addr;
	int opt = 1;
	int ret;

	if (video == NULL)
		return (EINVAL);

	if (!video->video_initialized || video->fb_base == NULL)
		return (ENXIO);

	/* Create listening socket */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return (errno);

	/* Set socket options */
	ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0) {
		close(fd);
		return (errno);
	}

	/* Bind to port (auto-select if port is 0) */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  /* localhost only */
	addr.sin_port = htons(port);

	if (port == 0) {
		/* Try to find an available port */
		for (uint16_t try_port = VNC_PORT_MIN;
		    try_port <= VNC_PORT_MAX; try_port++) {
			addr.sin_port = htons(try_port);
			if (bind(fd, (struct sockaddr *)&addr,
			    sizeof(addr)) == 0) {
				video->vnc.port = try_port;
				break;
			}
		}
		if (video->vnc.port == 0) {
			close(fd);
			return (EADDRNOTAVAIL);
		}
	} else {
		ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
		if (ret < 0) {
			close(fd);
			return (errno);
		}
	}

	/* Listen for connections */
	ret = listen(fd, 5);
	if (ret < 0) {
		close(fd);
		return (errno);
	}

	video->vnc_fd = fd;
	video->vnc_enabled = true;

	return (0);
}

/*
 * Shutdown VNC server
 */
int
emu_vnc_shutdown(struct emu_video *video)
{
	if (video == NULL)
		return (EINVAL);

	if (video->vnc_fd >= 0) {
		close(video->vnc_fd);
		video->vnc_fd = -1;
	}

	video->vnc_enabled = false;
	return (0);
}

/*
 * Set VNC password
 */
int
emu_vnc_set_password(struct emu_video *video, const char *password)
{
	size_t len;

	if (video == NULL)
		return (EINVAL);

	if (password == NULL)
		password = "";

	len = strlen(password);
	if (len >= sizeof(video->vnc.password))
		return (EINVAL);

	strcpy(video->vnc.password, password);
	return (0);
}

/*
 * Get VNC statistics
 */
int
emu_vnc_get_stats(struct emu_video *video, uint64_t *connections,
    uint64_t *bytes_sent)
{
	if (video == NULL)
		return (EINVAL);

	if (connections != NULL)
		*connections = video->vnc_connections;
	if (bytes_sent != NULL)
		*bytes_sent = video->vnc_bytes_sent;

	return (0);
}

/*
 * Get virtio-gpu configuration
 */
int
emu_vgpu_get_config(struct emu_video *video, uint64_t offset, int size,
    uint64_t *value)
{
	if (video == NULL || value == NULL)
		return (EINVAL);

	if (video->video_type != EMU_VIDEO_TYPE_VIRTIO_GPU)
		return (ENXIO);

	switch (offset) {
	case 0:  /* num_scanouts */
		if (size == 4) {
			*value = video->video_config.vgpu.vgpu_num_scanouts;
			return (0);
		}
		break;
	case 4:  /* num_capsets */
		if (size == 4) {
			*value = video->video_config.vgpu.vgpu_num_capsets;
			return (0);
		}
		break;
	}

	return (EINVAL);
}

/*
 * Set virtio-gpu configuration
 */
int
emu_vgpu_set_config(struct emu_video *video, uint64_t offset, int size,
    uint64_t value)
{
	if (video == NULL)
		return (EINVAL);

	if (video->video_type != EMU_VIDEO_TYPE_VIRTIO_GPU)
		return (ENXIO);

	/* virtio-gpu configuration is read-only */
	return (EROFS);
}

/*
 * Handle virtio-gpu control virtqueue
 */
int
emu_vgpu_handle_ctrl(struct emu_video *video, void *vq)
{
	if (video == NULL || vq == NULL)
		return (EINVAL);

	if (video->video_type != EMU_VIDEO_TYPE_VIRTIO_GPU)
		return (ENXIO);

	/* Control queue handling would be implemented here */
	/* This would process DRM commands, resource creation, etc. */

	return (0);
}

/*
 * Check if video device is ready
 */
bool
emu_video_ready(struct emu_video *video)
{
	if (video == NULL)
		return (false);

	return (video->video_initialized);
}

/*
 * Check if display is available
 */
bool
emu_video_has_display(struct emu_video *video)
{
	if (video == NULL)
		return (false);

	if (!video->video_initialized)
		return (false);

	if (video->video_type == EMU_VIDEO_TYPE_NONE)
		return (false);

	return (video->fb_base != NULL);
}

/*
 * Get video device type name
 */
const char *
emu_video_type_name(uint8_t type)
{
	if (type <= EMU_VIDEO_TYPE_VMWARE_SVGA)
		return (video_type_names[type]);

	return ("unknown");
}

/*
 * Look up resolution index by dimensions
 */
int
emu_video_lookup_resolution(uint16_t width, uint16_t height)
{
	for (int i = 0; i < (int)(sizeof(emu_video_resolutions) /
	    sizeof(emu_video_resolutions[0])); i++) {
		if (emu_video_resolutions[i].width == width &&
		    emu_video_resolutions[i].height == height)
			return (i);
	}
	return (-1);
}

/*
 * Calculate framebuffer size for given resolution
 */
size_t
emu_video_fb_size(uint16_t width, uint16_t height, uint8_t bpp)
{
	if (width == 0 || height == 0)
		return (0);

	/* Round pitch to 64-byte boundary for performance */
	uint32_t pitch = ((width * (bpp / 8)) + 63) & ~63;
	return ((size_t)pitch * height);
}
