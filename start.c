/*
 * CC430F5137 based photofinish - start part
 */

#include "io430.h"
#include "common.h"
#include "display.h"
#include "utils.h"
#include "rf_utils.h"
#include "rf_buff.h"
#include "packet.h"
#include "wc.h"

static struct rf_buff g_rf;
static struct wc_ctx  g_wc;
static int            g_show_clock;
static unsigned       g_start_offset;

static unsigned char select_channel(void)
{
	unsigned char ch;
	// Channels scan loop
	display_set_dp(1);
	for (ch = 0;; ++ch) {
		unsigned long cnt;
		if (ch == CTL_CHANNEL)
			// Skip control channel
			continue;
		display_clr_(2, 2);
		display_hex_(ch, 0, 2);
		rf_set_channel(ch);
		rf_rx_on();
		__delay_cycles(500000);
		display_hex_(rf_rssi(), 2, 2);
		rf_rx_off();
		for (cnt = 500000; cnt; --cnt) {
			if (!(P1IN & BTN_BIT))
				goto out;
		}
	}
out:
	wait_btn_release();
	return ch;
}

static void reset_channel(unsigned char ch)
{
	rf_set_channel(ch);
	rfb_send_msg(&g_rf, pkt_reset);
}

static void test_channel(unsigned char ch, unsigned char flags)
{
	unsigned ts;

	// Show channel number
	display_msg("Ch");
	display_set_dp(1);
	display_hex_(ch, 2, 2);

	// Send setup message via control channel
	rf_set_channel(CTL_CHANNEL);
	g_rf.tx.setup.chan  = ch;
	g_rf.tx.setup.flags = flags;
	ts = g_wc.ticks;
	rfb_send_msg(&g_rf, pkt_setup);

	// Switch to working channel
	rf_set_channel(ch);

	if (!(flags & SETUP_F_TEST))
		P1OUT |= BEEP_BIT;

	// Wait response message
	rfb_receive_msg_checked(&g_rf, pkt_setup_resp);
	// Calculate transmission delay
	g_start_offset = (g_wc.ticks - ts - SETUP_RESP_DELAY) / 2;

	P1OUT &= ~BEEP_BIT;

	// Handshake completed, show signal strength info
	display_set_dp(1);
	display_hex_(g_rf.rx.li.rssi, 0, 2);
	display_hex_(g_rf.rx.p.setup_resp.li.rssi, 2, 2);
}

static int wait_btn_cb()
{
	return (P1IN & BTN_BIT) ? 0 : -1;
}

typedef enum {
	mode_normal,
	/* Resume mode is to reconnect to finish which is already listening on the particular channel.
	 * The start just send reset packet to the finish and then follows standard startup routine.
	 */
	mode_resume,
	/* In test mode the start iteratively choosing successive channels and sending setup packets.
	 * The finish is just reset itself after responding to the setup packet.
	 */
	mode_test,
} start_mode_t;

#define MODE_SELECT_DELAY (3*SHORT_DELAY_TICKS)

int main( void )
{
	start_mode_t mode = mode_normal;
	unsigned char ch = 0;

	stop_watchdog();
	setup_ports();
	setup_clock();
	rf_init(sizeof(struct packet));
	configure_watchdog();
	__enable_interrupt();

	// Show battery voltage on start
	display_vcc();

	while (!(P1IN & BTN_BIT)) {
		/*
		 * Powering on with start button pressed activates mode selection.
		 */
		if (!wait_btn_release_tout(&g_wc, MODE_SELECT_DELAY))
			break;
		mode = mode_resume;
		display_msg("reSu");
		if (!wait_btn_release_tout(&g_wc, MODE_SELECT_DELAY))
			break;
		mode = mode_test;
		display_msg("teSt");
		break;
	}

	// Wait button press to start channels scan
	wait_btn();

	for (;;) {
		if (mode != mode_test)
			// Allow user to select channel
			ch = select_channel();

		// Set current clock as sesson id
		rfb_init_master(&g_rf, g_wc.ticks);

		if (mode == mode_resume) {
			reset_channel(ch);
			wc_delay(&g_wc, SHORT_DELAY_TICKS);
		}

		// Test selected channel
		test_channel(ch, mode == mode_test ? SETUP_F_TEST : 0);

		if (mode != mode_test)
			break;

		wc_delay(&g_wc, SHORT_DELAY_TICKS);

		// Autoincrement channel
		do { ++ch; } while (ch == CTL_CHANNEL);
	}

	for (;;) {
		int r;
		unsigned ts;
		// Wait status message or the start button press
		if (!(r = rfb_receive_valid_msg_(&g_rf, pkt_status, wait_btn_cb)))
		{
			// Status message received
			if (g_rf.rx.p.status.flags & sta_no_ir) {
				display_msg("noIr");
				P1OUT |= BEEP_BIT;
				wc_delay(&g_wc, SHORT_DELAY_TICKS);
				P1OUT &= ~BEEP_BIT;
			} else
				display_msg("Good");
			continue;
		}
		else if (r > 0) {
			rfb_err_msg(r);
			continue;
		}
		/*
		 * Start button pressed
		 */
		// Reset clock
		wc_reset(&g_wc);
		ts = g_wc.ticks;

		// Display clock
		display_set_dp(1);
		g_show_clock = 1;

		// Send start message
		P1OUT |= BEEP_BIT;
		++g_rf.tx.sn;
		for (r = REPEAT_MSGS; r; --r) {
			g_rf.tx.start.offset = g_start_offset + (g_wc.ticks - ts);
			rfb_send_msg(&g_rf, pkt_start);
			wc_delay(&g_wc, REPEAT_MSGS_DELAY);
		}
		P1OUT &= ~BEEP_BIT;

		// Wait finish message
		r = rfb_receive_valid_msg(&g_rf, pkt_finish);

		// Show result
		g_show_clock = 0;
		if (r & (err_proto|err_session)) {
			rfb_err_msg(r);
		} else if (r & err_timeout) {
			display_msg("----");
			if (r & err_crc)
				display_set_dp_mask(~0);
		} else {
			// Show reported time
			display_hex(g_rf.rx.p.finish.time);
			if (r & err_crc)
				display_set_dp_mask(~0);
		}

		// Short beep on finish
		P1OUT |= BEEP_BIT;
		wc_delay(&g_wc, SHORT_DELAY_TICKS);
		P1OUT &= ~BEEP_BIT;
	}
}

// Watchdog Timer interrupt service routine
#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
	if (wc_update(&g_wc) && g_show_clock)
		display_bin(g_wc.d);
	display_refresh();
	__low_power_mode_off_on_exit();
}
