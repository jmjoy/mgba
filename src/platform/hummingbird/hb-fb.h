#ifndef HB_FB_H
#define HB_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct HBFb {
	int fd;
	uint32_t* mem;
	size_t memLen;
	int width;
	int height;
	int stridePx;
};

bool hbFbOpen(struct HBFb* fb);
void hbFbClose(struct HBFb* fb);
void hbFbClear(struct HBFb* fb, uint32_t argb);

int hbHideConsoleCursor(void);
void hbRestoreConsoleCursor(int ttyFd);

static inline uint32_t* hbFbRow(struct HBFb* fb, int y) {
	return fb->mem + (size_t) y * fb->stridePx;
}

#endif
