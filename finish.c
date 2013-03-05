/*
 * CC430F5137 based photofinish - finish part
 */

#include "io430.h"
#include "common.h"
#include "display.h"
#include "utils.h"
#include "rf_utils.h"
#include "rf_buff.h"
#include "photosync.h"
#include "wc.h"

static struct phs_ctx g_phs;
static struct rf_buff g_rf;
static struct wc_ctx  g_wc;

// Global state
typedef enum {
	st_setup,
	st_calibrating,
	st_started,
	st_stopped,
} state_t;

static state_t g_state;
static int     g_timeout;

// Setup working channel
static void setup_channel()
{
	int r;

	g_state = st_setup;

	// Select control channel
	rf_set_channel(CTL_CHANNEL);

	// Wait setup message
	r = rfb_receive_msg(&g_rf, pkt_setup);
	if (r) {
		// Show error message
		rfb_err_msg(r);
		// Reset itself
		wc_delay(&g_wc, 1000);
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

static int wait_start_worker(struct rf_buff* ctx)
{
	// Run photosensor
	phs_run(&g_phs);

	if (g_state == st_calibrating)
	{
		if (g_phs.detected) {
			// Restart detection
			phs_restart(&g_phs);
			phs_set_mode(&g_phs, 1, 1);
			P1OUT |= BEEP_BIT;
		} else if (g_phs.ready)
			P1OUT &= ~BEEP_BIT;

		// Show current range
		display_set_dp(0);
		display_hex_(g_phs.sht, 0, 1);

		// Show current signal 
		if (g_phs.overload)
			display_msg_("---", 1, 3);
		else if (g_phs.ready)
			display_hex_(aver_value(&g_phs.asignal[1]), 1, 3);
		else
			display_clr_(1, 3);
	}

	// Refresh in sync with detection instead of the interrupt
	// to avoid interference via power consumption of the LEDs
	display_refresh();

	// Sleep till next clock tick
	__low_power_mode_2();

	return 0;
}

static void wait_start()
{
	for (;;) {
		// Wait setup message
		int r = rfb_receive_msg_(&g_rf, pkt_start, wait_start_worker);
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
	for (;;)
	{
		if (g_timeout)
			return;

		// Run photosensor
		phs_run(&g_phs);

		if (g_phs.detected)
			return;

		// Refresh in sync with detection instead of the interrupt
		// to avoid interference via power consumption of the LEDs
		display_refresh();

		// Sleep till next clock tick
		__low_power_mode_2();
	}
}

static void display_refresh_worker(struct rf_buff* ctx)
{
	display_refresh();
	// Sleep till next clock tick
	__low_power_mode_2();
}

static void report_finish()
{
	P1OUT |= BEEP_BIT;

	if (g_timeout) {
		display_msg("----");
		g_rf.tx.err |= err_timeout;
		g_rf.tx.finish.time = 0;
	} else
		g_rf.tx.finish.time = wc_get_time(&g_wc);

	rfb_send_msg_(&g_rf, pkt_finish, display_refresh_worker);

	P1OUT &= ~BEEP_BIT;
}

int main( void )
{
	stop_watchdog();
	setup_ports();
	setup_clock();
	rf_init(sizeof(struct packet));
	configure_watchdog();
	__enable_interrupt();

	// Show battery voltage on start
	display_vcc();

	// Setup RF channel
	setup_channel();

	// Init photosensor
	phs_init(&g_phs);
	phs_set_mode(&g_phs, 1, 1);
	g_state = st_calibrating;

	// Start/stop loop
	for (;;) {
		wait_start();
		phs_set_mode(&g_phs, 0, 1);
		g_state = st_started;
		detect_finish();
		phs_set_mode(&g_phs, 1, 0);
		g_state = st_stopped;
		report_finish();
	}
}

// Watchdog Timer interrupt service routine
#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
	int r = wc_update(&g_wc);
	if (r && g_state == st_started) {
		display_set_dp(1);
		display_bin(g_wc.d);
		if (r > WC_DIGITS)
			g_timeout = 1;
	}
	if (g_state == st_setup)
		display_refresh();
	__low_power_mode_off_on_exit();
}
