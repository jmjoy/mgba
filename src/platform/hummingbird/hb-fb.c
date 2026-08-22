/* Copyright (c) 2026 Loongson 2K0300 Hummingbird frontend for mGBA
 * Licensed under MPL v2.0, consistent with mGBA. */
#include "hb-fb.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define HB_TTY_DEVICE "/dev/tty0"
#define HB_KDSETMODE 0x4B3A
#define HB_KD_TEXT 0x00
#define HB_KD_GRAPHICS 0x01

bool hbFbOpen(struct HBFb* fb) {
	memset(fb, 0, sizeof(*fb));
	fb->fd = -1;

	int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("hb-fb: open /dev/fb0");
		return false;
	}

	struct fb_var_screeninfo vi;
	struct fb_fix_screeninfo fi;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
		perror("hb-fb: FBIOGET_VSCREENINFO");
		close(fd);
		return false;
	}
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
		perror("hb-fb: FBIOGET_FSCREENINFO");
		close(fd);
		return false;
	}

	if (vi.bits_per_pixel != 32) {
		fprintf(stderr, "hb-fb: unsupported bpp %u\n", vi.bits_per_pixel);
		close(fd);
		return false;
	}
	if ((vi.xres == 0 || vi.yres == 0) || fi.line_length < (unsigned) vi.xres * 4) {
		fprintf(stderr, "hb-fb: bogus geometry %ux%u stride %u\n", vi.xres, vi.yres, fi.line_length);
		close(fd);
		return false;
	}

	fb->fd = fd;
	fb->width = vi.xres;
	fb->height = vi.yres;
	fb->stridePx = fi.line_length / 4;

	fb->memLen = fi.smem_len ? fi.smem_len : (size_t) fi.line_length * vi.yres_virtual;
	fb->mem = mmap(NULL, fb->memLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb->mem == MAP_FAILED) {
		perror("hb-fb: mmap");
		fb->mem = NULL;
		close(fd);
		fb->fd = -1;
		return false;
	}

	printf("hb-fb: %dx%d stride %d px (%zu bytes)\n", fb->width, fb->height, fb->stridePx, fb->memLen);
	return true;
}

void hbFbClose(struct HBFb* fb) {
	if (fb->mem) {
		munmap(fb->mem, fb->memLen);
		fb->mem = NULL;
	}
	if (fb->fd >= 0) {
		close(fb->fd);
		fb->fd = -1;
	}
}

void hbFbClear(struct HBFb* fb, uint32_t argb) {
	for (int y = 0; y < fb->height; ++y) {
		uint32_t* row = hbFbRow(fb, y);
		for (int x = 0; x < fb->width; ++x) {
			row[x] = argb;
		}
	}
}

int hbHideConsoleCursor(void) {
	int fd = open(HB_TTY_DEVICE, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		return -1;
	}
	if (ioctl(fd, HB_KDSETMODE, (void*) (uintptr_t) HB_KD_GRAPHICS) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

void hbRestoreConsoleCursor(int ttyFd) {
	if (ttyFd < 0) {
		return;
	}
	ioctl(ttyFd, HB_KDSETMODE, (void*) (uintptr_t) HB_KD_TEXT);
	close(ttyFd);
}
