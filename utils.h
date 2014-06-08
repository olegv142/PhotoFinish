#pragma once

#include "io430.h"
#include "common.h"

static inline void stop_watchdog()
{
	// Stop watchdog timer to prevent time out reset
	WDTCTL = WDTPW + WDTHOLD;
}

static inline void configure_watchdog()
{
	// Interrupt rate 6.5MHz / 8192 = 793.5 Hz (1.26 msec)
	WDTCTL = WDTPW + WDTTMSEL + WDTSSEL__ACLK + WDTCNTCL + WDTIS__8192;
	SFRIE1 |= WDTIE; // Enable WDT interrupt
}

static inline void configure_timer_38k()
{
	TA0CCR0 = 86; // Interrupt twice during the 38kHz frequency period
	TA0CCTL0 = CCIE;
	TA0CTL = TASSEL_2 | MC__UP; // SMCLK UP to CCR0
}

static inline void timer_38k_enable(int en)
{
	if (en) {
		TA0CTL |= TACLR;
		TA0CCTL0 = CCIE;
	} else
		TA0CCTL0 = 0;
}

static inline void reset(void)
{
	PMMCTL0_H  = PMMPW_H;
	PMMCTL0_L |= PMMSWBOR;
}

static inline void stop(void)
{
	for (;;) __no_operation();
}

void stabilize_clock();

void setup_clock();

void setup_ports();

unsigned measure_vcc();

void set_vcore_up(unsigned level);

void set_vcore(unsigned level);

static inline void wait_btn_press()
{
	while (P1IN & BTN_BIT)
		__no_operation();
}

static inline void wait_btn_release()
{
	unsigned cnt;
	for (cnt = ~0; cnt; --cnt)
		if (!(P1IN & BTN_BIT))
			cnt = ~0;
}

struct wc_ctx;

int wait_btn_release_tout(struct wc_ctx* wc, unsigned ticks);

static inline void wait_btn()
{
	wait_btn_press();
	wait_btn_release();
}

static inline unsigned pack4nibbles(unsigned char nibbles[4])
{
	return nibbles[0] | (nibbles[1] << 4) | (nibbles[2] << 8) | (nibbles[3] << 12);
}

static inline void unpack4nibbles(unsigned packed, unsigned char nibbles[4])
{
	nibbles[0] = packed & 0xf;
	nibbles[1] = (packed >> 4) & 0xf;
	nibbles[2] = (packed >> 8) & 0xf;
	nibbles[3] = (packed >> 12) & 0xf;
}
