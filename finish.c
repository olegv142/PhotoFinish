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
	st_started,
	st_stopped,
} state_t;

static state_t  g_state;
static int      g_timeout;
static int      g_ir_timer;
static int      g_ir_burst;
static int      g_no_ir;
static unsigned g_no_ir_gen;
static int      g_beep;

static int is_calibrating(void)
{
	return !(P1IN & CALIB_SW);
}

static inline void beep(int duration)
{
	g_beep = duration;
}

static inline void beep_on(void)
{
	g_beep = -1;
}

static inline void beep_off(void)
{
	g_beep = 0;
}

// Timer0 A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void TIMER0_A0_ISR(void)
{
	// IR burst generation
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

static void set_state(state_t st)
{
	g_state = st;
}

static int ir_divider()
{
	if (g_state == st_idle)
		return 0;
	if (g_state == st_started)
		return 1;
	if (g_state == st_setup || is_calibrating())
		return 10;
	return WD_HZ;
}

#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
	// Routine maintenance tasks
	int ir_div = ir_divider();
	int r = wc_update(&g_wc);
	if (r && g_state == st_started) {
		display_set_dp(1);
		display_bin(g_wc.d);
		if (r > WC_DIGITS)
			g_timeout = 1;
	}
	display_refresh();
	if (is_calibrating()) {
		P1OUT |= CALIB_LED;
	} else {
		P1OUT &= ~CALIB_LED;
	}
	if (g_no_ir) {
		g_no_ir = 0;
		++g_no_ir_gen;
		if (is_calibrating())
			beep(ir_div * 2);
	}
	if (g_beep) {
		if (g_beep > 0)
			--g_beep;
		P1OUT |= BEEP_BIT;
	} else {
		P1OUT &= ~BEEP_BIT;
	}
	if (ir_div) {
		if (++g_ir_timer >= ir_div) {
			g_ir_timer -= ir_div;
			g_ir_burst = IR_BURST_PULSES * 2;
		}
	}
	__low_power_mode_off_on_exit();
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

	beep_on();

	// Set working channel
	rf_set_channel(g_rf.rx.p.setup.chan);

	// Delay to allow sender to switch to RX
	wc_delay(&g_wc, SETUP_RESP_DELAY);

	// Send test message
	g_rf.tx.setup_resp.li = g_rf.rx.li;
	rfb_send_msg(&g_rf, pkt_setup_resp);

	beep_off();

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
		int r = rfb_receive_valid_msg_(&g_rf, -1, chk_ir_cb);
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
		if (r) {
			continue;
		}
		switch (g_rf.rx.p.type) {
		case pkt_start:
			g_timeout = 0;
			wc_reset(&g_wc);
			wc_advance(&g_wc, g_rf.rx.p.start.offset);
			return;
		case pkt_reset:
			// Start wants to reinitialize communication
			reset();
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

	beep_on();

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

	beep_off();
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
		beep(SHORT_DELAY_TICKS);
		detect_finish();
		set_state(st_stopped);
		report_finish();
	}
}

