#pragma once

// The radio module interface helper.
// It is using low level API from RF1A module.
// The latter is taken from examples without
// any significant modifications.

#include "smartrf_CC1101.h"
#include "RF1A.h"
#include "utils.h"

#define RF_WHITENING   0x40 // Enabled
#define RF_PATABLE_VAL 0xc2 // Max power

static inline void rf_configure(unsigned char pktlen)
{
	//
	// The defines we are using here ar generated SmartRF Studio 7 freely available from TI
	//
	WriteSingleReg(FSCTRL1,  SMARTRF_SETTING_FSCTRL1);
	WriteSingleReg(FREQ2,    SMARTRF_SETTING_FREQ2);
	WriteSingleReg(FREQ1,    SMARTRF_SETTING_FREQ1);
	WriteSingleReg(FREQ0,    SMARTRF_SETTING_FREQ0);
	WriteSingleReg(MDMCFG4,  SMARTRF_SETTING_MDMCFG4);
	WriteSingleReg(MDMCFG3,  SMARTRF_SETTING_MDMCFG3);
	WriteSingleReg(MDMCFG2,  SMARTRF_SETTING_MDMCFG2);
	WriteSingleReg(DEVIATN,  SMARTRF_SETTING_DEVIATN);
	WriteSingleReg(MCSM0 ,   SMARTRF_SETTING_MCSM0);
	WriteSingleReg(FOCCFG,   SMARTRF_SETTING_FOCCFG);
	WriteSingleReg(FSCAL3,   SMARTRF_SETTING_FSCAL3);
	WriteSingleReg(FSCAL2,   SMARTRF_SETTING_FSCAL2);
	WriteSingleReg(FSCAL1,   SMARTRF_SETTING_FSCAL1);
	WriteSingleReg(FSCAL0,   SMARTRF_SETTING_FSCAL0);
	WriteSingleReg(TEST2,    SMARTRF_SETTING_TEST2);
	WriteSingleReg(TEST1,    SMARTRF_SETTING_TEST1);
	WriteSingleReg(FIFOTHR,  SMARTRF_SETTING_FIFOTHR);
	WriteSingleReg(IOCFG0,   SMARTRF_SETTING_IOCFG0);
	WriteSingleReg(PKTCTRL0, SMARTRF_SETTING_PKTCTRL0|RF_WHITENING);
	WriteSingleReg(PKTLEN,   pktlen);
}

static inline void rf_init(unsigned char pktlen)
{
	// Required for radio
	set_vcore(2);
	ResetRadioCore();

	// Set the High-Power Mode Request Enable bit so LPM3 can be entered
	// with active radio enabled 
	PMMCTL0_H = 0xA5;
	PMMCTL0_L |= PMMHPMRE_L; 
	PMMCTL0_H = 0x00; 

	rf_configure(pktlen);

	WriteSinglePATable(RF_PATABLE_VAL);
}

static inline unsigned char rf_get_state(void)
{
	return (Strobe(RF_SNOP) >> 4) & 7;
}

static inline void rf_wait_idle(void)
{
	while (rf_get_state()) __no_operation();
}

static inline void rf_off(void)
{
	Strobe(RF_SIDLE);
	rf_wait_idle();
}

static inline void rf_set_channel(unsigned char ch)
{
	rf_off();
	WriteSingleReg(CHANNR, ch);
}

static inline unsigned char rf_rssi(void)
{
	return (signed char)ReadSingleReg(RSSI) + 0x80;
}

static inline void rf_tx(unsigned char *buffer, unsigned char length)
{
	WriteBurstReg(RF_TXFIFOWR, buffer, length);
	RF1AIES |= BIT9;
	RF1AIFG &= ~BIT9;
	Strobe(RF_STX);
}

static inline int rf_tx_test(void)
{
	// Using interrupt flags is quite poorly documented.
	// The code is taken from examples and it works.
	return RF1AIFG & BIT9;
}

static inline void rf_rx_read(unsigned char *buffer, unsigned char length)
{
	ReadBurstReg(RF_RXFIFORD, buffer, length);
}

static inline void rf_rx_on(void)
{
	RF1AIES |= BIT9;
	RF1AIFG &= ~BIT9;
	// Radio is in IDLE following a TX, so strobe SRX to enter Receive Mode
	Strobe(RF_SRX);
}

static inline int rf_rx_test(void)
{
	// Using interrupt flags is quite poorly documented.
	// The code is taken from examples and it works.
	return RF1AIFG & BIT9;
}

static inline void rf_rx_off(void)
{
	// It is possible that ReceiveOff is called while radio is receiving a packet.
	// Therefore, it is necessary to flush the RX FIFO after issuing IDLE strobe
	// such that the RXFIFO is empty prior to receiving a packet.
	Strobe(RF_SIDLE);
	Strobe(RF_SFRX);
	rf_wait_idle();
}
