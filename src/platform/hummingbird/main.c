/* Copyright (c) 2026 Loongson 2K0300 Hummingbird frontend for mGBA
 * Licensed under MPL v2.0, consistent with mGBA. */
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>

#include "hb-fb.h"
#include "hb-touch.h"
#include "hb-ui.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t s_running = 1;

static void _handleSignal(int sig) {
	(void) sig;
	s_running = 0;
}

static void _makeSavePath(const char* romPath, char* out, size_t len) {
	snprintf(out, len, "%s", romPath);
	char* dot = strrchr(out, '.');
	char* slash = strrchr(out, '/');
	if (dot && (!slash || dot > slash)) {
		snprintf(dot, out + len - dot, ".sav");
	} else {
		size_t end = strlen(out);
		snprintf(out + end, len - end, ".sav");
	}
}

int main(int argc, char** argv) {
	const char* romPath = NULL;
	const char* touchDev = NULL;
	int i;
	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--touch") == 0 && i + 1 < argc) {
			touchDev = argv[++i];
		} else if (!romPath) {
			romPath = argv[i];
		}
	}

	if (!romPath) {
		fprintf(stderr, "Usage: %s [--touch /dev/input/eventN] ROM\n", argv[0]);
		return 1;
	}

	signal(SIGINT, _handleSignal);
	signal(SIGTERM, _handleSignal);
	signal(SIGHUP, SIG_IGN);

	struct HBFb fb;
	if (!hbFbOpen(&fb)) {
		return 1;
	}
	int ttyFd = hbHideConsoleCursor();
	hbFbClear(&fb, 0xFF000000u);

	if (hbTouchOpen(touchDev) < 0) {
		fprintf(stderr, "hb: continuing without touch input\n");
	}

	struct mCore* core = mCoreFind(romPath);
	if (!core) {
		fprintf(stderr, "hb: no core supports %s\n", romPath);
		return 1;
	}
	core->init(core);
	mCoreInitConfig(core, "hummingbird");
	mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");
	mCoreConfigSetDefaultValue(&core->config, "logToStdout", "true");
	mCoreConfigSetDefaultValue(&core->config, "mute", "true");
	mCoreLoadConfig(core);

	struct mStandardLogger logger;
	mStandardLoggerInit(&logger);
	mStandardLoggerConfig(&logger, &core->config);
	mLogSetDefaultLogger(&logger.d);

	if (!mCoreLoadFile(core, romPath)) {
		fprintf(stderr, "hb: failed to load %s\n", romPath);
		return 1;
	}

	char savePath[PATH_MAX];
	_makeSavePath(romPath, savePath, sizeof(savePath));
	struct VFile* save = VFileOpen(savePath, O_CREAT | O_RDWR);
	if (save) {
		if (!core->loadSave(core, save)) {
			save->close(save);
		} else {
			printf("hb: battery save %s\n", savePath);
		}
	}

	unsigned bw = 256;
	unsigned bh = 224;
	core->baseVideoSize(core, &bw, &bh);
	uint32_t* vbuf = malloc(bw * bh * sizeof(uint32_t));
	if (!vbuf) {
		fprintf(stderr, "hb: OOM video buffer\n");
		return 1;
	}
	core->setVideoBuffer(core, vbuf, bw);

	struct HBUI ui;

	core->reset(core);

	unsigned gw = bw;
	unsigned gh = bh;
	core->currentVideoSize(core, &gw, &gh);
	hbUiInit(&ui, fb.width, fb.height, gw, gh);

	double fps = 59.7275;
	int32_t freq = core->frequency(core);
	int32_t cyclesPerFrame = core->frameCycles(core);
	if (freq > 0 && cyclesPerFrame > 0) {
		fps = (double) freq / cyclesPerFrame;
	}
	uint64_t frameNs = (uint64_t) (1000000000.0 / fps);
	printf("hb: core %s %ux%u scale x%d -> %dx%d @(%d,%d) target %.4f fps\n",
	       core->platform(core) == mPLATFORM_GB ? "GB" : "GBA",
	       gw, gh, ui.gs, ui.gw, ui.gh, ui.gx, ui.gy, fps);

	int b;
	for (b = 0; b < HB_BTN_COUNT; ++b) {
		ui.b[b].drawn = true;
	}
	hbUiDraw(&fb, &ui);

	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);

	uint64_t frames = 0;
	uint64_t statFrames = 0;
	uint32_t lastKeys = 0xFFFFFFFFu;
	struct timespec statTime = next;

	while (s_running) {
		hbTouchPoll();
		uint32_t keys = hbUiComputeKeys(&ui);
		if (keys != lastKeys) {
			printf("hb: keys -> 0x%08x\n", (unsigned) keys);
			fflush(stdout);
			lastKeys = keys;
		}

		for (b = 0; b < HB_BTN_COUNT; ++b) {
			ui.b[b].pressed = (keys & ui.b[b].keyBit) != 0;
		}

		core->setKeys(core, keys);
		core->runFrame(core);

		const void* pixels = NULL;
		size_t stride = 0;
		core->getPixels(core, &pixels, &stride);
		if (pixels) {
			hbUiBlitGame(&fb, &ui, pixels, stride);
		}
		hbUiDraw(&fb, &ui);

		next.tv_nsec += frameNs;
		while (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			++next.tv_sec;
		}
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int64_t diff = (int64_t) (next.tv_sec - now.tv_sec) * 1000000000LL + next.tv_nsec - now.tv_nsec;
		if (diff > 0) {
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		} else if (diff < -200000000LL) {
			next = now;
		}

		++frames;
		++statFrames;
		long elapsed = now.tv_sec - statTime.tv_sec;
		if (elapsed >= 10) {
			double actual = statFrames / (double) elapsed;
			printf("hb: %" PRIu64 " frames, %.2f fps actual\n", frames, actual);
			fflush(stdout);
			statFrames = 0;
			statTime = now;
		}
	}

	printf("hb: shutting down after %" PRIu64 " frames\n", frames);
	hbRestoreConsoleCursor(ttyFd);
	hbTouchClose();
	hbUiDeinit(&ui);
	free(vbuf);
	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	hbFbClose(&fb);
	return 0;
}
