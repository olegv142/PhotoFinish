/*
 * CC430F5137 based photofinish - finish part
 */

#include "io430.h"
#include "common.h"
#include "display.h"
#include "utils.h"
#include "rf_utils.h"
#include "rf_buff.h"
#include "wc.h"

static struct rf_buff g_rf;
static struct wc_ctx  g_wc;

// Global state
typedef enum {
	st_idle,
	st_setup,
	st_calibrating,
	st_started,
	st_stopped,
} state_t;

static state_t g_state;
static state_t g_next_state;
static int     g_timeout;
static int     g_ir_burst;
static int     g_has_ir;
static int     g_no_ir;
static int     g_no_ir_cnt;
static int     g_beep;

// Timer0 A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void TIMER0_A0_ISR(void)
{
	if (!g_ir_burst)
		return;
	--g_ir_burst;
	if (g_ir_burst & 1)
		P1OUT |= IR_BITS;
	else
		P1OUT &= ~IR_BITS;
	if (!g_ir_burst) {
		/* Last pulse in burst ended */
		if (P2IN & RX_BIT) {
			g_no_ir = 1;
			++g_no_ir_cnt;
		} else
			g_has_ir = 1;
	}
}

static void set_state_(state_t st)
{
	switch (g_state) {
	case st_calibrating:
		g_beep = 0;
		P1OUT &= ~BEEP_BIT;
		P1OUT &= ~CALIB_LED;
		break;
	}
	switch (st) {
	case st_calibrating:
		P1OUT |= CALIB_LED;
		break;
	case st_started:
		g_beep = SHORT_DELAY_TICKS;
		P1OUT |= BEEP_BIT;
		break;
	}
	g_state = st;
}

#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
	int r;
	if (g_next_state != g_state)
		set_state_(g_next_state);
	r = wc_update(&g_wc);
	if (r && g_state == st_started) {
		display_set_dp(1);
		display_bin(g_wc.d);
		if (r > WC_DIGITS)
			g_timeout = 1;
	}
	display_refresh();
	if (g_state == st_stopped) {
		if (!(P1IN & CALIB_BTN))
			set_state_(g_next_state = st_calibrating);
	}
	if (g_state == st_calibrating) {
		if (g_no_ir) {
			g_no_ir = 0;
			P1OUT |= BEEP_BIT;
		}
		if (g_has_ir) {
			g_has_ir = 0;
			P1OUT &= ~BEEP_BIT;
		}
	}
	if (g_beep && !--g_beep)
		P1OUT &= ~BEEP_BIT;
	if (g_state == st_calibrating || g_state == st_started) {
		timer_38k_enable(1);
		g_ir_burst = IR_BURST_PULSES * 2;
	}
	__low_power_mode_off_on_exit();
}

static void set_state(state_t st)
{
	while (st != g_state) g_next_state = st;
}

// Setup working channel
static void setup_channel()
{
	int r;

	set_state(st_setup);

	// Select control channel
	rf_set_channel(CTL_CHANNEL);

	// Wait setup message
	r = rfb_receive_msg(&g_rf, pkt_setup);
	if (r) {
		// Show error message
		rfb_err_msg(r);
		// Reset itself
		wc_delay(&g_wc, SHORT_DELAY_TICKS);
		reset();
	} else {
		// Show channel number
		display_msg("Ch");
		display_set_dp(1);
		display_hex_(g_rf.rx.p.setup.chan, 2, 2);
	}

	P1OUT |= BEEP_BIT;

	// Set working channel
	rf_set_channel(g_rf.rx.p.setup.chan);

	// Delay to allow sender to switch to RX
	wc_delay(&g_wc, SETUP_RESP_DELAY);

	// Send test message
	g_rf.tx.setup_resp.li = g_rf.rx.li;
	rfb_send_msg(&g_rf, pkt_setup_resp);

	P1OUT &= ~BEEP_BIT;

	// Reset itself on error or in test mode
	if (r || (g_rf.rx.p.setup.flags & SETUP_F_TEST))
		reset();
}

static void wait_start()
{
	for (;;) {
		// Wait start message
		int r = rfb_receive_valid_msg(&g_rf, pkt_start);
		if (!(r & (err_proto|err_session)))
		{
			wc_reset(&g_wc);
			g_timeout = 0;
			if (!r)
				wc_advance(&g_wc, g_rf.rx.p.start.offset);
			return;
		}
	}
}

static void detect_finish()
{
	g_no_ir = 0;

	for (;;)
	{
		if (g_timeout)
			return;

		if (g_no_ir)
			return;

		// Sleep till next clock tick
		__low_power_mode_2();
	}
}

static void check_ir()
{
	int no_ir_cnt = g_no_ir_cnt;
	set_state(st_calibrating);
	wc_delay(&g_wc, SHORT_DELAY_TICKS);
	if (no_ir_cnt == g_no_ir_cnt)
		set_state(st_stopped);
}

static void report_finish()
{
	int i;

	P1OUT |= BEEP_BIT;

	if (g_timeout) {
		display_msg("----");
		g_rf.tx.err |= err_timeout;
		g_rf.tx.finish.time = 0;
	} else
		g_rf.tx.finish.time = wc_get_time(&g_wc);

	display_hex(g_rf.tx.finish.time);

	for (i = REPEAT_MSGS; i; --i) {
		rfb_send_msg(&g_rf, pkt_finish);
		wc_delay(&g_wc, REPEAT_MSGS_DELAY);
	}

	P1OUT &= ~BEEP_BIT;
}

int main( void )
{
	stop_watchdog();
	setup_ports();
	setup_clock();
	rf_init(sizeof(struct packet));
	configure_timer_38k();
	configure_watchdog();

	P1DIR &= ~CALIB_BTN;
	P1REN |= CALIB_BTN;
	P1OUT |= CALIB_BTN;
	P2DIR &= ~RX_BIT;

	__enable_interrupt();

	// Show battery voltage on start
	display_vcc();

	// Setup RF channel
	setup_channel();

	set_state(st_calibrating);

	// Start/stop loop
	for (;;) {
		wait_start();
		set_state(st_started);
		detect_finish();
		set_state(st_stopped);
		report_finish();
		check_ir();
	}
}

