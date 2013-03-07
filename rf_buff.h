#pragma once

#include "packet.h"
#include "rf_utils.h"

/* The protocol buffer */
struct rf_buff {
	struct packet      tx;
	struct packet_buff rx;
	int                master;
};

/*
 * We are using synchronous approach here.
 * The functions will wait till operation completion. Functions with _ suffix will
 * call idle callback passed as the last parameter to support background activities.
 */

void rfb_send_msg_(struct rf_buff* rf, unsigned char type, void (*cb)(void));
int rfb_receive_msg_(struct rf_buff* rf, int type, int (*cb)(void));
int rfb_chk_rx_err(struct rf_buff* rf, int type);
void rfb_err_msg(int err);

static inline void rfb_init_master(struct rf_buff* rf, unsigned char se)
{
	rf->master = 1;
	rf->tx.se = se;
}

static inline void rfb_send_msg(struct rf_buff* rf, unsigned char type)
{
	rfb_send_msg_(rf, type, 0);
}

static inline int rfb_receive_msg(struct rf_buff* rf, int type)
{
	return rfb_receive_msg_(rf, type, 0);
}

static inline int rfb_receive_valid_msg_(struct rf_buff* rf, int type, int (*cb)(void))
{
	for (;;) {
		int res = rfb_receive_msg_(rf, type, cb);
		if (res != err_crc)
			return res;
	}
}

static inline int rfb_receive_valid_msg(struct rf_buff* rf, int type)
{
	return rfb_receive_valid_msg_(rf, type, 0);
}

static inline void rfb_receive_msg_checked(struct rf_buff* rf, int type)
{
	int r = rfb_receive_msg(rf, type);
	if (r > 0) {
		rfb_err_msg(r);
		stop();
	}
}

