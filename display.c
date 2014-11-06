#include "io430.h"
#include "display.h"

static unsigned char disp_i;
static unsigned char disp_seg[DISP_SEGS];
static unsigned char disp_dp;

enum {
	_a_ = 1,
	_b_ = 2,
	_c_ = 4,
	_d_ = 8,
	_e_ = 16,
	_f_ = 32,
	_g_ = 64,
	_p_ = 128,
};

static unsigned char disp_char_map[] = {
	
	['0'] = _a_|_b_|_c_|_d_|_e_|_f_ ,
	['1'] = _b_|_c_,
	['2'] = _a_|_b_|_g_|_e_|_d_,
	['3'] = _a_|_b_|_c_|_d_|_g_,
	['4'] = _f_|_g_|_b_|_c_,
	['5'] = _a_|_f_|_g_|_c_|_d_,
	['6'] = _a_|_f_|_g_|_c_|_d_|_e_,
	['7'] = _a_|_b_|_c_,
	['8'] = _a_|_b_|_c_|_d_|_e_|_f_|_g_,
	['9'] = _a_|_b_|_c_|_d_|_f_|_g_,
	['A'] = _a_|_b_|_c_|_e_|_f_|_g_,
	['-'] = _g_,
	['a'] = _a_|_b_|_c_|_d_|_e_|_g_,
	['P'] = _e_|_f_|_a_|_b_|_g_,
	['b'] = _f_|_e_|_d_|_c_|_g_,
	['C'] = _a_|_f_|_e_|_d_,
	['c'] = _g_|_e_|_d_,
	['d'] = _b_|_c_|_d_|_e_|_g_,
	['E'] = _a_|_f_|_e_|_d_|_g_,
	['e'] = _g_|_b_|_a_|_f_|_e_|_d_,
	['F'] = _a_|_f_|_g_|_e_,
	['G'] = _a_|_f_|_e_|_d_|_c_,
	['g'] = _a_|_b_|_c_|_d_|_f_|_g_,
	['U'] = _f_|_e_|_d_|_c_|_b_,
	['u'] = _e_|_d_|_c_,
	['v'] = _e_|_d_|_c_,
	['r'] = _g_|_e_,
	['H'] = _b_|_c_|_e_|_f_|_g_,
	['h'] = _f_|_e_|_g_|_c_,
	['L'] = _f_|_e_|_d_,
	['l'] = _f_|_e_|_d_,
	['S'] = _a_|_f_|_g_|_c_|_d_,
	['n'] = _e_|_g_|_c_,
	['I'] = _f_|_e_,
	['O'] = _a_|_b_|_c_|_d_|_e_|_f_,
	['o'] = _c_|_d_|_e_|_g_,
	['t'] = _f_|_e_|_d_|_g_,
	['?'] = _a_|_b_|_g_|_e_,
	['`'] = _f_|_g_,
};

void display_test()
{
 	unsigned char seg, pos;
	for (pos = 1; pos & 0xf; pos <<= 1) {
		DISP_DIG_OUT = pos;
		for (seg = 1; seg; seg <<= 1) {
			DISP_SEG_OUT = ~seg;
			__delay_cycles(50000);
		}
		DISP_SEG_OUT = ~0;
	}
	DISP_DIG_OUT = 0;
}

void display_msg_(const char* msg, int pos, int len)
{
	int i, e;
	for (i = pos, e = pos + len; i < e; ++i) {
		if (*msg) {
			disp_seg[i] = disp_char_map[(unsigned char)*msg];
			++msg;
		} else
			disp_seg[i] = 0;
	}
	disp_dp = 0;
}

void display_refresh()
{
	unsigned dig;
	if (++disp_i >= DISP_SEGS)
		disp_i = 0;
	dig = 1 << disp_i;
	DISP_DIG_OUT = 0;
	DISP_SEG_OUT = ~(disp_seg[disp_i] | (disp_dp & dig ? _p_ : 0));
	DISP_DIG_OUT = dig;
}

void display_set_dp(int pos)
{
	if (pos < 0)
		disp_dp = 0;
	else
		disp_dp = 1 << pos;
}

void display_set_dp_mask(unsigned mask)
{
	disp_dp = mask;
}

static unsigned char map_bin(unsigned char val)
{
	if (val < 10)
		return disp_char_map['0' + val];
	if (val < 16) {
		unsigned char m;
		if ((m = disp_char_map['A' + val - 10]))
			return m;
		return disp_char_map['a' + val - 10];
	}
	return disp_char_map['?'];
}

void display_clr_(int pos, int len)
{
	int i;
	for (i = pos + len - 1; i >= pos; --i)
		disp_seg[i] = 0;
}

void display_bin_(const unsigned char* val, int pos, int len)
{
	int i;
	for (i = pos + len - 1; i >= pos; --i)
		disp_seg[i] = map_bin(*val++);
}

void display_dec(unsigned val)
{
	int i;
	for (i = DISP_SEGS - 1; i >= 0; --i) {
		unsigned val_ = val / 10, d = val - val_ * 10;
		disp_seg[i] = disp_char_map['0' + d];
		val = val_;
	}
}

