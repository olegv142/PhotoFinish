#pragma once

#include "utils.h"

/* Wall clock module */

#define WC_DIGITS 4

/* We are expecting 1586.9Hz update rate (26MHz / (32*512)) */
#define WC_DIV 15869
/* We are measuring time in 1/100 sec units */
#define WC_DELTA 1000

struct wc_ctx {
	unsigned char d[WC_DIGITS];
	unsigned cnt;
	unsigned volatile ticks;
};

static inline void wc_reset(struct wc_ctx* wc)
{
	int i;
	for (i = 0; i < WC_DIGITS; ++i)
		wc->d[i] = 0;
	wc->cnt = 0;
}

static inline int wc_tick(struct wc_ctx* wc)
{
	int i;
	wc->cnt += WC_DELTA;
	if (wc->cnt < WC_DIV)
		return 0;
	wc->cnt -= WC_DIV;
	for (i = 0; i < WC_DIGITS; ++i)
		if (++wc->d[i] < 10)
			return i + 1;
		else
			wc->d[i] = 0;
	return WC_DIGITS + 1;
}

static inline int wc_update(struct wc_ctx* wc)
{
	++wc->ticks;
	return wc_tick(wc);
}

static inline int wc_advance(struct wc_ctx* wc, int ticks)
{
	int r, res = 0;
	for (; ticks; --ticks)
		if ((r = wc_tick(wc)) > res)
			res = r;
	return res;
}

static inline unsigned wc_get_time(struct wc_ctx* wc)
{
	return pack4nibbles(wc->d);
}

static inline void wc_set_time(struct wc_ctx* wc, unsigned time)
{
	unpack4nibbles(time, wc->d);
}

static inline void wc_delay(struct wc_ctx* wc, unsigned ticks)
{
	unsigned expired = wc->ticks + ticks;
	while (wc->ticks != expired)
		__no_operation();
}
