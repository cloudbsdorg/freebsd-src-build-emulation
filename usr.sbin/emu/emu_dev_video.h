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

#ifndef _EMU_DEV_VIDEO_H_
#define	_EMU_DEV_VIDEO_H_

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * emu_dev_video - Standalone Video/Framebuffer Device
 *
 * Provides framebuffer display and VNC server for the emulation framework.
 * Supports multiple video modes and resolutions.
 *
 * Features:
 * - Configurable resolution (640x480 to 3840x2160)
 * - Multiple video device types (stdvga, cirrus, virtio-gpu)
 * - VNC server for remote display
 * - Password protection for VNC
 * - Multiple simultaneous VNC connections
 */

/* Video device types */
#define	EMU_VIDEO_TYPE_NONE		0
#define	EMU_VIDEO_TYPE_STDVGA		1
#define	EMU_VIDEO_TYPE_CIRRUS		2
#define	EMU_VIDEO_TYPE_VIRTIO_GPU	3
#define	EMU_VIDEO_TYPE_VMWARE_SVGA	4

/* Default resolutions */
#define	EMU_VIDEO_RES_640X480		0
#define	EMU_VIDEO_RES_800X600		1
#define	EMU_VIDEO_RES_1024X768		2
#define	EMU_VIDEO_RES_1280X720		3
#define	EMU_VIDEO_RES_1280X800		4
#define	EMU_VIDEO_RES_1366X768		5
#define	EMU_VIDEO_RES_1920X1080		6
#define	EMU_VIDEO_RES_1920X1200		7
#define	EMU_VIDEO_RES_2560X1440		8
#define	EMU_VIDEO_RES_3840X2160		9

/* Video resolution table - width, height, bits per pixel */
struct emu_video_resolution {
	uint16_t	width;
	uint16_t	height;
	uint8_t	bpp;
	uint32_t	pitch;		/* Bytes per row */
	uint32_t	fb_size;	/* Total framebuffer size */
};

/* VNC server configuration */
struct emu_vnc_config {
	uint16_t	port;		/* VNC port (0 = auto) */
	char		password[64];	/* VNC password (empty = no auth) */
	uint8_t		read_only;	/* Read-only mode */
	uint8_t		shared;		/* Allow shared connections */
};

/* virtio-gpu configuration space */
struct emu_vgpu_config {
	uint32_t	vgpu_num_scanouts;	/* Number of scanouts */
	uint32_t	vgpu_num_capsets;	/* Number of capability sets */
};

/* Standard VGA configuration */
struct emu_stdvga_config {
	uint16_t	width;
	uint16_t	height;
	uint32_t	vram_size;	/* Video RAM size in bytes */
	bool		vmware_compat;	/* VMware compatibility mode */
};

/* Cirrus configuration */
struct emu_cirrus_config {
	uint16_t	width;
	uint16_t	height;
	uint32_t	vram_size;	/* Video RAM (8MB max) */
	bool		blink;		/* Enable cursor blink */
};

/* Video device context */
struct emu_video {
	/* Device type */
	uint8_t			video_type;

	/* Configuration */
	union {
		struct emu_vgpu_config	vgpu;
		struct emu_stdvga_config	stdvga;
		struct emu_cirrus_config	cirrus;
	} video_config;

	/* Framebuffer */
	void			*fb_base;	/* Framebuffer memory */
	size_t			fb_size;	/* Framebuffer size */
	uint16_t		fb_width;	/* Current width */
	uint16_t		fb_height;	/* Current height */
	uint8_t			fb_bpp;		/* Bits per pixel */
	uint32_t		fb_pitch;	/* Bytes per row */

	/* Display state */
	uint16_t		dirty_x1;	/* Dirty region top-left */
	uint16_t		dirty_y1;
	uint16_t		dirty_x2;	/* Dirty region bottom-right */
	uint16_t		dirty_y2;
	bool			dirty_valid;

	/* VNC server */
	struct emu_vnc_config	vnc;
	bool			vnc_enabled;
	int			vnc_fd;		/* VNC listening socket */
	void			*vnc_thread;	/* VNC thread handle */

	/* Virtqueue reference */
	void			*ctrl_vq;	/* Control virtqueue */
	void			*cursor_vq;	/* Cursor virtqueue */

	/* Device state */
	bool			video_initialized;
	bool			display_enabled;

	/* Statistics */
	uint64_t		frames_rendered;
	uint64_t		vnc_connections;
	uint64_t		vnc_bytes_sent;
	uint64_t		display_updates;
};

/* Resolution lookup table */
extern const struct emu_video_resolution emu_video_resolutions[];

/* Video device operations */
int	emu_video_init(struct emu_video *video, uint8_t type);
void	emu_video_destroy(struct emu_video *video);
void	emu_video_reset(struct emu_video *video);

/* Framebuffer operations */
int	emu_video_set_resolution(struct emu_video *video, uint16_t width,
		    uint16_t height, uint8_t bpp);
int	emu_video_get_resolution(struct emu_video *video, uint16_t *width,
		    uint16_t *height, uint8_t *bpp);
int	emu_video_blit(struct emu_video *video, const void *data,
		    size_t len, uint32_t x, uint32_t y,
		    uint32_t w, uint32_t h);
int	emu_video_fill_rect(struct emu_video *video, uint32_t x, uint32_t y,
		    uint32_t w, uint32_t h, uint32_t color);

/* Display operations */
int	emu_video_enable(struct emu_video *video);
int	emu_video_disable(struct emu_video *video);
int	emu_video_refresh(struct emu_video *video);
int	emu_video_flush(struct emu_video *video);

/* VNC server operations */
int	emu_vnc_init(struct emu_video *video, uint16_t port);
int	emu_vnc_shutdown(struct emu_video *video);
int	emu_vnc_set_password(struct emu_video *video, const char *password);
int	emu_vnc_get_stats(struct emu_video *video, uint64_t *connections,
		    uint64_t *bytes_sent);

/* Virtio-gpu specific operations */
int	emu_vgpu_get_config(struct emu_video *video, uint64_t offset,
		    int size, uint64_t *value);
int	emu_vgpu_set_config(struct emu_video *video, uint64_t offset,
		    int size, uint64_t value);
int	emu_vgpu_handle_ctrl(struct emu_video *video, void *vq);

/* Status queries */
bool	emu_video_ready(struct emu_video *video);
bool	emu_video_has_display(struct emu_video *video);

/* Utility functions */
const char *emu_video_type_name(uint8_t type);
int	emu_video_lookup_resolution(uint16_t width, uint16_t height);
size_t	emu_video_fb_size(uint16_t width, uint16_t height, uint8_t bpp);

#endif /* !_EMU_DEV_VIDEO_H_ */
