#pragma once

#include "utils.h"

/* Display driver */

#define DISP_SEGS 4
#define DISP_DIG_OUT PJOUT
#define DISP_SEG_OUT P3OUT

void display_test();
void display_refresh();
void display_dec(unsigned val);
void display_set_dp(int pos);
void display_set_dp_mask(unsigned mask);
void display_msg_(const char* msg, int pos, int len);
void display_bin_(const unsigned char* val, int pos, int len);
void display_clr_(int pos, int len);

static inline void display_msg(const char* msg)
{
	display_msg_(msg, 0, DISP_SEGS);
}

static inline void display_bin(const unsigned char* val)
{
	display_bin_(val, 0, DISP_SEGS);
}

static inline void display_clr()
{
	display_clr_(0, DISP_SEGS);
}

static inline void display_hex_(unsigned val, int pos, int len)
{
	unsigned char buff[DISP_SEGS];
	unpack4nibbles(val, buff);
	display_bin_(buff, pos, len);
}

static inline void display_hex(unsigned val)
{
	display_hex_(val, 0, DISP_SEGS);
}

static inline void display_vcc()
{
	display_set_dp(0);
	display_dec(measure_vcc());
}
