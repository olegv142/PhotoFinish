#include "io430.h"
#include "rf_buff.h"
#include "display.h"

void rfb_send_msg_(struct rf_buff* rf, unsigned char type, void (*cb)(void))
{
	rf->tx.type = type;
	if (!rf->master)
		rf->tx.sn = rf->rx.p.sn;
	rf_tx((unsigned char*)&rf->tx, sizeof(rf->tx));
	rf->tx.err = 0;
	while (!rf_tx_test())
		if (cb) cb();
}

int rfb_chk_rx_err(struct rf_buff* rf, int type)
{
	if (!rf->rx.li.crc_ok)
		return err_crc;
	if (type >= 0 && rf->rx.p.type != type)
		return err_proto;
	if (rf->rx.p.type != pkt_setup && rf->rx.p.se != rf->tx.se || (rf->master && rf->rx.p.sn != rf->tx.sn))
		return err_session;
	if (!rf->master && rf->rx.p.type == pkt_setup)
		rf->tx.se = rf->rx.p.se;
	if (rf->rx.p.err)
		return rf->rx.p.err | err_remote;
	return 0;
}

int rfb_receive_msg_(struct rf_buff* rf, int type, int (*cb)(void))
{
	int err;
	rf_rx_on();
	while (!rf_rx_test()) {
		if (cb && 0 > (err = cb())) {
			rf_rx_off();
			return err;
		}
	}
	rf_rx_read((unsigned char*)&rf->rx, sizeof(rf->rx));
	if ((err = rfb_chk_rx_err(rf, type)) && !rf->master)
		// Errors will be reported to master
		rf->tx.err |= err;
	return err;
}

void rfb_err_msg(int err)
{
	if (err < 0)
		return;
	if (err & err_crc)
		display_msg("ErrC");
	else if (err & err_proto)
		display_msg("ErrP");
	else if (err & err_session)
		display_msg("ErrS");
}

