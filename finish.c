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

static state_t  g_state;
static state_t  g_next_state;
static int      g_timeout;
static int      g_sec_div;
static int      g_ir_burst;
static int      g_no_ir;
static unsigned g_no_ir_gen;
static int      g_beep;

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
		if (P2IN & RX_BIT)
			g_no_ir = 1;
	}
}

static void set_state_(state_t st)
{
	g_beep = 0;
	P1OUT &= ~BEEP_BIT;
	P1OUT &= ~CALIB_LED;
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
	if (!(P1IN & CALIB_SW)) {
		if (g_state == st_stopped)
			set_state_(g_next_state = st_calibrating);
	} else {
		if (g_state == st_calibrating)
			set_state_(g_next_state = st_stopped);
	}
	if (g_state == st_calibrating) {
		if (g_no_ir)
			P1OUT |= BEEP_BIT;
		else
			P1OUT &= ~BEEP_BIT;
	} else if (g_no_ir) {
		++g_no_ir_gen;
	}
	g_no_ir = 0;
	if (g_state == st_calibrating || g_state == st_started || (g_state == st_stopped && !g_sec_div)) {
		g_ir_burst = IR_BURST_PULSES * 2;
	}
	if (g_sec_div) {
		--g_sec_div;
	} else {
		g_sec_div = WD_HZ;
	}
	if (g_beep && !--g_beep) {
		P1OUT &= ~BEEP_BIT;
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

/*
 * IR barrier health monitoring stuff
 */
static unsigned g_no_ir_gen_;
static unsigned g_no_ir_expire;
static int      g_no_ir_reported;

static void chk_ir_ctx_init()
{
	g_no_ir_gen_   = g_no_ir_gen;
	g_no_ir_expire = g_wc.ticks + 10 * WD_HZ;
}

static int chk_ir_cb()
{
	if (g_state == st_calibrating) {
		chk_ir_ctx_init();
		return 0;
	}
	if (g_no_ir_gen_ != g_no_ir_gen) {
		if (!g_no_ir_reported)
			return -1;
		chk_ir_ctx_init();
	} else {
		if (g_no_ir_reported && (int)(g_wc.ticks - g_no_ir_expire) > 0)
			return -1;
	}
	return 0;
}

static void wait_start()
{
	for (;;) {
		// Wait start message monitoring IR at the same time
		chk_ir_ctx_init();
		int r = rfb_receive_valid_msg_(&g_rf, pkt_start, chk_ir_cb);
		if (r < 0) {
			// Need to send IR status to start
			if (!g_no_ir_reported) {
				display_msg("noIr");
				g_rf.tx.status.flags = sta_no_ir;
				rfb_send_msg(&g_rf, pkt_status);
				g_no_ir_reported = 1;
			} else {
				display_msg("Good");
				g_rf.tx.status.flags = 0;
				rfb_send_msg(&g_rf, pkt_status);
				g_no_ir_reported = 0;
			}
			continue;
		}
		if (r == err_proto && g_rf.rx.p.type == pkt_reset) {
			// Start wants to reinitialize communication
			reset();
		}
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
	unsigned no_ir_gen = g_no_ir_gen;

	for (;;)
	{
		if (g_timeout)
			return;

		if (no_ir_gen != g_no_ir_gen)
			return;

		// Sleep till next clock tick
		__low_power_mode_2();
	}
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
	timer_38k_enable(1);
	configure_watchdog();

	P1DIR &= ~CALIB_SW;
	P1REN |= CALIB_SW;
	P1OUT |= CALIB_SW;
	P2DIR &= ~RX_BIT;

	__enable_interrupt();

	// Show battery voltage on start
	display_vcc();

	// Setup RF channel
	setup_channel();

	set_state(st_stopped);

	// Start/stop loop
	for (;;) {
		wait_start();
		set_state(st_started);
		detect_finish();
		set_state(st_stopped);
		report_finish();
	}
}

