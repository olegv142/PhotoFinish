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
#include "nvram.h"
#include "wc.h"

// Uncomment to show signal strength indicator
//#define SHOW_RSSI

#define START_DEBOUNCE_TICKS 64

// Button events
enum {
	btn_start_pressed  = -1,
	btn_start_released = -2,
	btn_user           = -3,
};

static struct rf_buff g_rf;
static struct wc_ctx  g_wc;
static int            g_show_clock;
static unsigned       g_start_offset;

static volatile unsigned g_start_pressed;
static int               g_start_last_status;

static inline void beep_on()
{
	P1OUT |= BEEP_BIT;
}

static inline void beep_off()
{
	P1OUT &= ~BEEP_BIT;
}

static void show_channel_info(unsigned char ch)
{
	rf_set_channel(ch);
#ifdef SHOW_RSSI
	display_clr_(2, 2);
	display_hex_(ch, 0, 2);
	rf_rx_on();
	__delay_cycles(500000);
	display_hex_(rf_rssi(), 2, 2);
	rf_rx_off();
#else
	display_msg("Ch");
	display_hex_(ch, 2, 2);
	__delay_cycles(500000);
#endif
}

static unsigned char scan_select_channel(unsigned char start_ch)
{
	unsigned char ch;
	// Channels scan loop
	display_set_dp(1);
	for (ch = start_ch;; ++ch) {
		unsigned long cnt;
		if (ch == CTL_CHANNEL)
			// Skip control channel
			continue;
		show_channel_info(ch);
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
		beep_on();

	// Wait response message
	rfb_receive_msg_checked(&g_rf, pkt_setup_resp);
	// Calculate transmission delay
	g_start_offset = (g_wc.ticks - ts - SETUP_RESP_DELAY) / 2;

	beep_off();
#ifdef SHOW_RSSI
	// Handshake completed, show signal strength info
	display_set_dp(1);
	display_hex_(g_rf.rx.li.rssi, 0, 2);
	display_hex_(g_rf.rx.p.setup_resp.li.rssi, 2, 2);
#endif
}

// Start button debounce routine called from WDT ISR
static void start_btn_chk(void)
{
	if (!(P1IN & START_BTN_BIT)) {
		g_start_pressed = START_DEBOUNCE_TICKS;
	} else {
		unsigned cnt = g_start_pressed;
		if (cnt) {
			g_start_pressed = cnt - 1;
		}
	}
}

static int monitor_btns()
{
	int start_pressed = g_start_pressed != 0;
	if (!(P1IN & START_BTN_BIT)) {
		g_start_pressed = START_DEBOUNCE_TICKS;
		start_pressed = 1;
	}
	if (g_start_last_status != start_pressed) {
		g_start_last_status = start_pressed;
		return start_pressed ? btn_start_pressed : btn_start_released;
	}
	if (!(P1IN & BTN_BIT)) {
		return btn_user;
	}
	return 0;
}

static int monitor_user_btn()
{
	if (!(P1IN & BTN_BIT))
		return btn_user;
	return 0;
}

typedef enum {
	/* Resume mode is to reconnect to finish which is already listening on the particular channel.
	 * The start just send reset packet to the finish and then follows standard startup routine.
	 */
	mode_resume,
	/* Begin with choosing channel
	 */
	mode_scan,
	/* In test mode the start iteratively choosing successive channels and sending setup packets.
	 * The finish is just reset itself after responding to the setup packet.
	 */
	mode_test,
} start_mode_t;

#define MODE_SELECT_DELAY (3*SHORT_DELAY_TICKS)

struct stored_channel {
	unsigned char ch;
	unsigned char se;
};

static inline void save_channel(unsigned char ch, unsigned char se)
{
	struct stored_channel sch;
	sch.ch = ch;
	sch.se = se;
	// Remember channel and session id
	nv_put(&sch, sizeof(sch));
}

static void setup_start_ports( void )
{
	setup_ports();
	P1DIR &= ~START_BTN_BIT;
	P1REN |= START_BTN_BIT;
	P1OUT |= START_BTN_BIT;
	g_start_pressed = START_DEBOUNCE_TICKS;
	g_start_last_status = 1;
}

static void beep(void)
{
	beep_on();
	wc_delay(&g_wc, SHORT_DELAY_TICKS);
	beep_off();
}

static void start_and_wait(void)
{
	int r;
	unsigned ts;
	// Reset clock
	wc_reset(&g_wc);
	ts = g_wc.ticks;

	// Display clock
	display_set_dp(1);
	g_show_clock = 1;

	// Send start message
	beep_on();
	++g_rf.tx.sn;
	for (r = REPEAT_MSGS; r; --r) {
		g_rf.tx.start.offset = g_start_offset + (g_wc.ticks - ts);
		rfb_send_msg(&g_rf, pkt_start);
		wc_delay(&g_wc, REPEAT_MSGS_DELAY);
	}
	beep_off();

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
	beep();
}

int main( void )
{
	start_mode_t mode = mode_resume;
	struct stored_channel const* sch;
	unsigned char ch, se;

	stop_watchdog();
	setup_start_ports();
	setup_clock();
	rf_init(sizeof(struct packet));
	configure_watchdog();
	__enable_interrupt();

	// Show battery voltage on start
	display_vcc();

	while (!(P1IN & BTN_BIT)) {
		/*
		 * Powering on with button pressed activates mode selection.
		 */
		if (!wait_btn_release_tout(&g_wc, MODE_SELECT_DELAY))
			break;
		mode = mode_scan;
		display_msg("Scan");
		if (!wait_btn_release_tout(&g_wc, MODE_SELECT_DELAY))
			break;
		mode = mode_test;
		display_msg("teSt");
		wait_btn_release();
		break;
	}

	// Wait button press to start channels scan
	wait_btn();

	// Use current clock as sesson id
	se = g_wc.ticks;
	// Query stored channel info
	sch = nv_get(sizeof(*sch));
	switch (mode) {
	case mode_resume:
		if (sch) {
			// Use stored channel info
			ch = sch->ch;
			se = sch->se;
			show_channel_info(ch);
			break;
		}
	case mode_scan:
		// Allow user to select new channel
		ch = scan_select_channel(sch ? sch->ch : 0);
		save_channel(ch, se);
		break;
	case mode_test:
		ch = 0;
		break;
	}

	for (;;) {
		rfb_init_master(&g_rf, se);

		if (mode == mode_resume) {
			reset_channel(ch);
			wc_delay(&g_wc, SHORT_DELAY_TICKS);
		}

		// Test selected channel
		test_channel(ch, mode == mode_test ? SETUP_F_TEST : 0);

		if (mode != mode_test)
			break;

		wc_delay(&g_wc, SHORT_DELAY_TICKS);
		se = g_wc.ticks;

		// Autoincrement channel
		do { ++ch; } while (ch == CTL_CHANNEL);
	}

	for (;;) {
		int r;
		// Wait status message or the start button press
		if (!(r = rfb_receive_valid_msg_(&g_rf, -1, monitor_btns)))
		{
			switch (g_rf.rx.p.type) {
			case pkt_status:
				// Status message received
				if (g_rf.rx.p.status.flags & sta_no_ir) {
					display_msg("noIr");
					beep();
				} else
					display_msg("Good");
				break;
			case pkt_ping:
				// Ping message received
				beep_on();
				display_rssi();
				wc_delay(&g_wc, SHORT_DELAY_TICKS);
				rfb_send_msg(&g_rf, pkt_ping);
				beep_off();
				break;
			}
			continue;
		}
		if (r == btn_user)
		{
			/* Send ping */
			for (;;) {
				wait_btn_release();
				beep_on();
				display_msg("PIng");
				rfb_send_msg(&g_rf, pkt_ping);
				r = rfb_receive_valid_msg_(&g_rf, pkt_ping, monitor_user_btn);
				if (!r) {
					display_rssi();
					beep_off();
					break;
				}
				if (r == btn_user) {
					beep_off();
					continue;
				}
				rfb_err_msg(r);
				break;
			}
			continue;
		}
		if (r == btn_start_pressed) {
			/* Start button pressed */
			start_and_wait();
			continue;
		}
		if (r == btn_start_released) {
			/* Start button released */
			beep();
			continue;
		}
		rfb_err_msg(r);
	}
}

// Watchdog Timer interrupt service routine
#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
	start_btn_chk();
	if (wc_update(&g_wc) && g_show_clock)
		display_bin(g_wc.d);
	display_refresh();
	__low_power_mode_off_on_exit();
}
