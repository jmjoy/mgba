#include "hb-touch.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct HBTouchPoint s_slots[HB_TOUCH_MAX_POINTS];
static int s_fd = -1;

int hbTouchOpen(const char* device) {
	if (!device) {
		device = "/dev/input/event0";
	}
	s_fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (s_fd < 0) {
		perror("hb-touch: open");
		return -1;
	}
	return 0;
}

void hbTouchClose(void) {
	if (s_fd >= 0) {
		close(s_fd);
		s_fd = -1;
	}
	memset(s_slots, 0, sizeof(s_slots));
}

void hbTouchPoll(void) {
	if (s_fd < 0) {
		return;
	}
	struct input_event ev;
	int slot = 0;
	for (;;) {
		ssize_t n = read(s_fd, &ev, sizeof(ev));
		if (n != (ssize_t) sizeof(ev)) {
			break;
		}
		switch (ev.type) {
		case EV_ABS:
			switch (ev.code) {
			case ABS_MT_SLOT:
				if (ev.value >= 0 && ev.value < HB_TOUCH_MAX_POINTS) {
					slot = ev.value;
				}
				break;
			case ABS_MT_TRACKING_ID:
				if (slot < HB_TOUCH_MAX_POINTS) {
					s_slots[slot].down = ev.value >= 0;
					if (!s_slots[slot].down) {
						s_slots[slot].x = -1;
						s_slots[slot].y = -1;
					}
				}
				break;
			case ABS_MT_POSITION_X:
				if (slot < HB_TOUCH_MAX_POINTS) {
					s_slots[slot].x = ev.value;
				}
				break;
			case ABS_MT_POSITION_Y:
				if (slot < HB_TOUCH_MAX_POINTS) {
					s_slots[slot].y = ev.value;
				}
				break;
			default:
				break;
			}
			break;
		case EV_SYN:
			if (ev.code == SYN_DROPPED) {
				memset(s_slots, 0, sizeof(s_slots));
			}
			break;
		default:
			break;
		}
	}
}

const struct HBTouchPoint* hbTouchPoints(int* outCount) {
	if (outCount) {
		*outCount = HB_TOUCH_MAX_POINTS;
	}
	return s_slots;
}
