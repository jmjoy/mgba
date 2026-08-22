#include "hb-ui.h"

#include "hb-fb.h"
#include "hb-touch.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HB_SPRITE_PAD 3
#define HB_FONT_W 5
#define HB_FONT_H 7
#define HB_QUAD_ROUND 12

struct HBGlyph {
	char ch;
	uint8_t rows[HB_FONT_H];
};

static const struct HBGlyph s_glyphs[] = {
	{ 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
	{ 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
	{ 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	{ 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
	{ 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
	{ 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
	{ '^', { 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04 } },
	{ 'v', { 0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04 } },
	{ '<', { 0x04, 0x08, 0x10, 0x1F, 0x10, 0x08, 0x04 } },
	{ '>', { 0x04, 0x02, 0x01, 0x1F, 0x01, 0x02, 0x04 } },
};

static const char s_quadArrows[4] = { '^', 'v', '<', '>' };
static const uint32_t s_quadKeys[4] = { 1u << 6, 1u << 7, 1u << 5, 1u << 4 };

static float _clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static float _shapeSdf(const struct HBButton* b, float dx, float dy) {
	if (b->shape == HB_CIRCLE) {
		return sqrtf(dx * dx + dy * dy) - b->rx;
	}
	if (b->shape == HB_QUADPAD) {
		float ax = fabsf(dx);
		float ay = fabsf(dy);
		float px = ax;
		float py = ay;
		if (py > px) {
			float tmp = px;
			px = py;
			py = tmp;
		}
		float qx = px - b->rx;
		float qy = py - b->ry;
		float k = qx > qy ? qx : qy;
		float dist;
		if (k > 0.0f) {
			float mx = qx > 0.0f ? qx : 0.0f;
			float my = qy > 0.0f ? qy : 0.0f;
			dist = sqrtf(mx * mx + my * my);
		} else {
			dist = -sqrtf((b->ry - px) * (b->ry - px) + k * k);
		}
		return dist - HB_QUAD_ROUND;
	}
	float r = b->ry;
	float qx = fabsf(dx) - (b->rx - r);
	float qy = fabsf(dy) - r;
	float ox = qx > 0.0f ? qx : 0.0f;
	float oy = qy > 0.0f ? qy : 0.0f;
	float inner = qx > qy ? qx : qy;
	return sqrtf(ox * ox + oy * oy) + (inner < 0.0f ? inner : 0.0f) - r;
}

static bool _glyphPixel(char ch, int gx, int gy) {
	size_t i;
	for (i = 0; i < sizeof(s_glyphs) / sizeof(s_glyphs[0]); ++i) {
		if (s_glyphs[i].ch == ch) {
			return (s_glyphs[i].rows[gy] >> (HB_FONT_W - 1 - gx)) & 1;
		}
	}
	return false;
}

static void _renderSprite(struct HBButton* b, uint32_t** out, bool pressed) {
	int W = b->rx * 2 + HB_SPRITE_PAD * 2;
	int H = b->ry * 2 + HB_SPRITE_PAD * 2;
	uint32_t* px = calloc(W * H, sizeof(uint32_t));

	float fillA = pressed ? 200.0f / 255.0f : 120.0f / 255.0f;
	float fillR = pressed ? 1.00f : 0.28f;
	float fillG = pressed ? 0.78f : 0.30f;
	float fillB = pressed ? 0.25f : 0.35f;

	float labA = pressed ? 240.0f / 255.0f : 190.0f / 255.0f;

	float cxf = (W - 1) / 2.0f;
	float cyf = (H - 1) / 2.0f;
	const char* lbl = b->label;
	int charCount = strlen(lbl);
	int textW = charCount * (HB_FONT_W + 1) - 1;
	int textH = HB_FONT_H;

	int y;
	for (y = 0; y < H; ++y) {
		int x;
		for (x = 0; x < W; ++x) {
			float dx = x - cxf;
			float dy = y - cyf;
			float d = _shapeSdf(b, dx, dy);
			float cov = _clampf(0.5f - d, 0.0f, 1.0f);
			if (cov <= 0.0f) {
				continue;
			}

			float r = fillR * fillA;
			float g = fillG * fillA;
			float bl = fillB * fillA;

			float lx = (x - cxf) + textW / 2.0f;
			float ly = (y - cyf) + textH / 2.0f;
			if (cov > 0.55f && lx >= -1.0f && ly >= -1.0f) {
				int gx = (int) lx;
				int gy = (int) ly;
				if (gy >= 0 && gy < textH && gx >= 0 && gx < textW) {
					int ch = gx / (HB_FONT_W + 1);
					int col = gx % (HB_FONT_W + 1);
					if (ch < charCount && col < HB_FONT_W && _glyphPixel(lbl[ch], col, gy)) {
						r += labA * (1.0f - fillA);
						g += labA * (1.0f - fillA);
						bl += labA * (1.0f - fillA);
					}
				}
			}

			uint32_t ir = (uint32_t) _clampf(r * cov * 255.0f + 0.5f, 0.0f, 255.0f);
			uint32_t ig = (uint32_t) _clampf(g * cov * 255.0f + 0.5f, 0.0f, 255.0f);
			uint32_t ib = (uint32_t) _clampf(bl * cov * 255.0f + 0.5f, 0.0f, 255.0f);
			px[y * W + x] = 0xFF000000u | (ir << 16) | (ig << 8) | ib;
		}
	}

	free(*out);
	*out = px;
	b->bw = W;
	b->bh = H;
}

static int _quadArm(float dx, float dy, float halfWid) {
	float ax = fabsf(dx);
	float ay = fabsf(dy);
	if (ax <= halfWid && ay <= halfWid) {
		return -1;
	}
	if (ay > ax) {
		return dy < 0.0f ? 0 : 1;
	}
	return dx < 0.0f ? 2 : 3;
}

static bool _quadInside(const struct HBButton* b, float dx, float dy) {
	float ax = fabsf(dx);
	float ay = fabsf(dy);
	return (ax <= b->rx && ay <= b->ry) || (ay <= b->rx && ax <= b->ry);
}

static void _renderQuad(struct HBUI* ui, uint32_t pressedMask) {
	struct HBButton* proto = NULL;
	int i;
	for (i = 0; i < HB_BTN_COUNT; ++i) {
		if (ui->b[i].shape == HB_QUADPAD) {
			proto = &ui->b[i];
			break;
		}
	}
	if (!proto || !ui->quadScratch) {
		return;
	}
	int W = proto->bw;
	int H = proto->bh;

	float cxf = (W - 1) / 2.0f;
	float cyf = (H - 1) / 2.0f;
	float midOff = proto->ry + (proto->rx - proto->ry) / 2.0f;

	int a;
	for (a = 0; a < H * W; ++a) {
		ui->quadScratch[a] = 0;
	}

	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			float dx = x - cxf;
			float dy = y - cyf;
			float d = _shapeSdf(proto, dx, dy);
			float cov = _clampf(0.5f - d, 0.0f, 1.0f);
			if (cov <= 0.0f) {
				continue;
			}

			int arm = _quadArm(dx, dy, proto->ry);
			bool hot = arm >= 0 && (pressedMask & s_quadKeys[arm]) != 0;

			float fillA = hot ? 200.0f / 255.0f : 120.0f / 255.0f;
			float fillR = hot ? 1.00f : 0.28f;
			float fillG = hot ? 0.78f : 0.30f;
			float fillB = hot ? 0.25f : 0.35f;

			float p1 = fabsf(dx - dy) * 0.70710678f;
			float p2 = fabsf(dx + dy) * 0.70710678f;
			float sd = p1 < p2 ? p1 : p2;
			if (sd < 3.5f && cov > 0.85f) {
				float t = 1.0f - sd / 3.5f;
				float k = 1.0f - 0.5f * t * t;
				fillR *= k;
				fillG *= k;
				fillB *= k;
			}

			float r = fillR * fillA;
			float g = fillG * fillA;
			float bl = fillB * fillA;

			float labA = hot ? 240.0f / 255.0f : 200.0f / 255.0f;
			for (a = 0; a < 4; ++a) {
				float ox = a == 3 ? midOff : (a == 2 ? -midOff : 0.0f);
				float oy = a == 0 ? -midOff : (a == 1 ? midOff : 0.0f);
				int gx = (int) floorf(dx - ox) + HB_FONT_W / 2;
				int gy = (int) floorf(dy - oy) + HB_FONT_H / 2;
				if (gx < 0 || gx >= HB_FONT_W || gy < 0 || gy >= HB_FONT_H) {
					continue;
				}
				if (!_glyphPixel(s_quadArrows[a], gx, gy)) {
					continue;
				}
				float la = (hot && arm == a) ? 240.0f / 255.0f : labA * 0.75f;
				float add = la * (1.0f - fillA);
				r += add;
				g += add;
				bl += add;
				break;
			}

			uint32_t ir = (uint32_t) _clampf(r * cov * 255.0f + 0.5f, 0.0f, 255.0f);
			uint32_t ig = (uint32_t) _clampf(g * cov * 255.0f + 0.5f, 0.0f, 255.0f);
			uint32_t ib = (uint32_t) _clampf(bl * cov * 255.0f + 0.5f, 0.0f, 255.0f);
			ui->quadScratch[y * W + x] = 0xFF000000u | (ir << 16) | (ig << 8) | ib;
		}
	}
}

void hbUiInit(struct HBUI* ui, int screenW, int screenH, int gameW, int gameH) {
	memset(ui, 0, sizeof(*ui));

	int availW = screenW / 2 - 32;
	int availH = screenH - 155;
	ui->gs = 1;
	while ((gameW * (ui->gs + 1)) <= availW && (gameH * (ui->gs + 1)) <= availH) {
		++ui->gs;
	}
	ui->gw = gameW * ui->gs;
	ui->gh = gameH * ui->gs;
	ui->gx = (screenW - ui->gw) / 2;
	ui->gy = 6 + (availH - ui->gh) / 2;

	struct HBButton* b = ui->b;
	b[HB_BTN_A] = (struct HBButton) { HB_CIRCLE, 938, 534, 38, 38, 1u << 0, "A" };
	b[HB_BTN_B] = (struct HBButton) { HB_CIRCLE, 846, 534, 38, 38, 1u << 1, "B" };
	b[HB_BTN_SELECT] = (struct HBButton) { HB_PILL, 461, 556, 42, 16, 1u << 2, "SELECT" };
	b[HB_BTN_START] = (struct HBButton) { HB_PILL, 563, 556, 42, 16, 1u << 3, "START" };
	b[HB_BTN_RIGHT] = (struct HBButton) { HB_QUADPAD, 150, 500, 60, 25, 1u << 4, ">" };
	b[HB_BTN_LEFT] = (struct HBButton) { HB_QUADPAD, 150, 500, 60, 25, 1u << 5, "<" };
	b[HB_BTN_UP] = (struct HBButton) { HB_QUADPAD, 150, 500, 60, 25, 1u << 6, "^" };
	b[HB_BTN_DOWN] = (struct HBButton) { HB_QUADPAD, 150, 500, 60, 25, 1u << 7, "v" };
	b[HB_BTN_R] = (struct HBButton) { HB_PILL, 884, 30, 58, 17, 1u << 8, "R" };
	b[HB_BTN_L] = (struct HBButton) { HB_PILL, 140, 30, 58, 17, 1u << 9, "L" };

	int i;
	for (i = 0; i < HB_BTN_COUNT; ++i) {
		ui->b[i].pressed = false;
		ui->b[i].drawn = false;
		if (ui->b[i].shape == HB_QUADPAD) {
			ui->b[i].bw = ui->b[i].rx * 2 + HB_SPRITE_PAD * 2;
			ui->b[i].bh = ui->b[i].rx * 2 + HB_SPRITE_PAD * 2;
			continue;
		}
		_renderSprite(&ui->b[i], &ui->b[i].sprUp, false);
		_renderSprite(&ui->b[i], &ui->b[i].sprDown, true);
	}
	ui->quadScratch = malloc(b[HB_BTN_UP].bw * b[HB_BTN_UP].bh * sizeof(uint32_t));
	ui->quadLastMask = -1;
}

void hbUiDeinit(struct HBUI* ui) {
	int i;
	for (i = 0; i < HB_BTN_COUNT; ++i) {
		free(ui->b[i].sprUp);
		free(ui->b[i].sprDown);
	}
	free(ui->quadScratch);
	memset(ui, 0, sizeof(*ui));
}

uint32_t hbUiComputeKeys(const struct HBUI* ui) {
	uint32_t keys = 0;
	int count;
	const struct HBTouchPoint* pts = hbTouchPoints(&count);
	int p;
	for (p = 0; p < count; ++p) {
		if (!pts[p].down) {
			continue;
		}
		int i;
		for (i = 0; i < HB_BTN_COUNT; ++i) {
			const struct HBButton* btn = &ui->b[i];
			float dx = pts[p].x - btn->cx;
			float dy = pts[p].y - btn->cy;
			bool hit;
			switch (btn->shape) {
			case HB_CIRCLE:
				hit = dx * dx + dy * dy <= (float) btn->rx * btn->rx;
				break;
			case HB_QUADPAD:
				hit = false;
				if (_quadInside(btn, dx, dy)) {
					int arm = _quadArm(dx, dy, btn->ry);
					hit = arm >= 0 && s_quadKeys[arm] == btn->keyBit;
				}
				break;
			default:
				hit = fabsf(dx) <= btn->rx && fabsf(dy) <= btn->ry;
				break;
			}
			if (hit) {
				keys |= btn->keyBit;
			}
		}
	}
	return keys;
}

void hbUiDraw(struct HBFb* fb, struct HBUI* ui) {
	uint32_t mask = 0;
	struct HBButton* proto = NULL;
	int i;
	for (i = 0; i < HB_BTN_COUNT; ++i) {
		struct HBButton* btn = &ui->b[i];
		if (btn->shape != HB_QUADPAD) {
			continue;
		}
		proto = btn;
		if (btn->pressed) {
			mask |= btn->keyBit;
		}
	}
	if (proto && mask != (uint32_t) ui->quadLastMask) {
		_renderQuad(ui, mask);
		int x0 = proto->cx - proto->bw / 2;
		int y0 = proto->cy - proto->bh / 2;
		int y;
		for (y = 0; y < proto->bh; ++y) {
			int fy = y0 + y;
			if (fy < 0 || fy >= fb->height) {
				continue;
			}
			uint32_t* dst = hbFbRow(fb, fy) + x0;
			memcpy(dst, ui->quadScratch + (size_t) y * proto->bw, proto->bw * sizeof(uint32_t));
		}
		ui->quadLastMask = (int) mask;
		for (i = 0; i < HB_BTN_COUNT; ++i) {
			if (ui->b[i].shape == HB_QUADPAD) {
				ui->b[i].drawn = true;
			}
		}
	}

	for (i = 0; i < HB_BTN_COUNT; ++i) {
		struct HBButton* btn = &ui->b[i];
		if (btn->shape == HB_QUADPAD) {
			continue;
		}
		if (btn->drawn == btn->pressed) {
			continue;
		}
		const uint32_t* spr = btn->pressed ? btn->sprDown : btn->sprUp;
		int x0 = btn->cx - btn->bw / 2;
		int y0 = btn->cy - btn->bh / 2;
		int y;
		for (y = 0; y < btn->bh; ++y) {
			int fy = y0 + y;
			if (fy < 0 || fy >= fb->height) {
				continue;
			}
			uint32_t* dst = hbFbRow(fb, fy) + x0;
			memcpy(dst, spr + (size_t) y * btn->bw, btn->bw * sizeof(uint32_t));
		}
		btn->drawn = btn->pressed;
	}
}

void hbUiBlitGame(struct HBFb* fb, const struct HBUI* ui, const uint32_t* src, size_t stride) {
	int j;
	for (j = 0; j < ui->gh; ++j) {
		const uint32_t* srow = src + (size_t) (j / ui->gs) * stride;
		uint32_t* drow = hbFbRow(fb, ui->gy + j) + ui->gx;
		int i;
		for (i = 0; i < ui->gw; ++i) {
			uint32_t p = srow[i / ui->gs];
			drow[i] = 0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
		}
	}
}
