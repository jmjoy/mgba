#ifndef HB_TOUCH_H
#define HB_TOUCH_H

#include <stdbool.h>

#define HB_TOUCH_MAX_POINTS 5

struct HBTouchPoint {
	bool down;
	int x;
	int y;
};

int hbTouchOpen(const char* device);
void hbTouchClose(void);
void hbTouchPoll(void);
const struct HBTouchPoint* hbTouchPoints(int* outCount);

#endif
