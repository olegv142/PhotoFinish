#include "utils.h"
#include "rf_utils.h"
#include "display.h"
#include "wc.h"

void stabilize_clock()
{
	// Loop until XT1,XT2 & DCO stabilizes
	while (SFRIFG1 & OFIFG) {	// Test oscillator fault flag
		P1OUT |= LED_BIT;
		UCSCTL7 &= ~(XT2OFFG + XT1LFOFFG + XT1HFOFFG + DCOFFG);
							// Clear XT2,XT1,DCO fault flags
		SFRIFG1 &= ~OFIFG;  // Clear fault flags
		__delay_cycles(1000);
	}
	P1OUT &= ~LED_BIT;
}

void setup_clock()
{
	// All clocks to 26/4=6.5 MHz
	UCSCTL6 &= ~XT2OFF;
	UCSCTL5 = DIVM__4 | DIVS__4 | DIVA__4 | DIVPA__4;
	UCSCTL4 = SELM__XT2CLK | SELS__XT2CLK | SELA__XT2CLK;
	UCSCTL3 = 0x70; // Disable FLL
	// Wait clock to settle
	stabilize_clock();
}

void setup_ports()
{
	// Configure default levels
	P1OUT = 0;
	P2OUT = 0;
	P3OUT = ~0;
	PJOUT = 0;
	// Configure all as output
	P1DIR = 0xff;
	P2DIR = 0xff;
	P3DIR = 0xff;
	PJDIR = 0xff;
	// Configure start button
	P1DIR &= ~BTN_BIT;
	P1REN |= BTN_BIT;
	P1OUT |= BTN_BIT;
}

unsigned measure_vcc()
{
	unsigned v;
	REFCTL0 = REFMSTR|REFON|REFVSEL_1; // 2V REF
	ADC12CTL0 = 0;
	ADC12CTL0  = ADC12ON|ADC12SHT0_4;
	ADC12CTL1  = ADC12SSEL_1|ADC12SHP|ADC12DIV_6; // ACLK/6 ~ 1.1MHz
	ADC12MCTL0 = ADC12INCH_11|ADC12SREF_1; // VCC/2
	ADC12CTL0 |= ADC12ENC;
	__delay_cycles(10000);
	ADC12CTL0 |= ADC12SC;
	while (!(ADC12IFG & ADC12IFG0))
		__no_operation();
	v = ADC12MEM0;
	ADC12CTL0 &= ~(ADC12ON|ADC12ENC);
	REFCTL0 = 0;
	// Here we have 4096 ~ 4V
	return v - (v >> 5);
}

void set_vcore_up(unsigned level)
{
	// Open PMM registers for write access
	PMMCTL0_H = PMMPW_H;
	// Set SVS/SVM high side new level
	SVSMHCTL = SVSHE + SVSHRVL0 * level + SVMHE + SVSMHRRL0 * level;
	// Set SVM low side to new level
	SVSMLCTL = SVSLE + SVMLE + SVSMLRRL0 * level;
	// Wait till SVM is settled
	while ((PMMIFG & SVSMLDLYIFG) == 0);
	// Clear already set flags
	PMMIFG &= ~(SVMLVLRIFG + SVMLIFG);
	// Set VCore to new level
	PMMCTL0_L = PMMCOREV0 * level;
	// Wait till new level reached
	if ((PMMIFG & SVMLIFG))
		while ((PMMIFG & SVMLVLRIFG) == 0);
	// Set SVS/SVM low side to new level
	SVSMLCTL = SVSLE + SVSLRVL0 * level + SVMLE + SVSMLRRL0 * level;
	// Lock PMM registers for write access
	PMMCTL0_H = 0;
}

void set_vcore(unsigned level)
{
	unsigned v;
	for (v = 1; v <= level; ++v)
		set_vcore_up(v);
}

int wait_btn_release_tout(struct wc_ctx* wc, unsigned ticks)
{
	unsigned cnt;
	unsigned expired = wc->ticks + ticks;
	for (cnt = ~0; cnt; --cnt) {
		if ((int)(wc->ticks - expired) > 0)
			return -1;
		if (!(P1IN & BTN_BIT))
			cnt = ~0;
	}
	return 0;
}

void display_rssi()
{
	display_hex_(rf_rssi(), 2, 2);
	display_msg_("`-", 0, 2);
}
