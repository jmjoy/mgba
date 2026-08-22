#ifndef HB_UI_H
#define HB_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct HBFb;

enum HBShape {
	HB_CIRCLE,
	HB_PILL,
	HB_QUADPAD
};

enum HBButtonId {
	HB_BTN_A = 0,
	HB_BTN_B,
	HB_BTN_SELECT,
	HB_BTN_START,
	HB_BTN_RIGHT,
	HB_BTN_LEFT,
	HB_BTN_UP,
	HB_BTN_DOWN,
	HB_BTN_R,
	HB_BTN_L,
	HB_BTN_COUNT
};

struct HBButton {
	enum HBShape shape;
	int cx;
	int cy;
	int rx;
	int ry;
	uint32_t keyBit;
	const char* label;
	bool pressed;
	bool drawn;
	uint32_t* sprUp;
	uint32_t* sprDown;
	int bw;
	int bh;
};

struct HBUI {
	struct HBButton b[HB_BTN_COUNT];
	int gw;
	int gh;
	int gs;
	int gx;
	int gy;
	uint32_t* quadScratch;
	int quadLastMask;
};

void hbUiInit(struct HBUI* ui, int screenW, int screenH, int gameW, int gameH);
void hbUiDeinit(struct HBUI* ui);
uint32_t hbUiComputeKeys(const struct HBUI* ui);
void hbUiDraw(struct HBFb* fb, struct HBUI* ui);
void hbUiBlitGame(struct HBFb* fb, const struct HBUI* ui, const uint32_t* src, size_t stride);

#endif
