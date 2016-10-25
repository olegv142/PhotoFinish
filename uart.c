#include "io430.h"
#include "uart.h"
#include "utils.h"

void setup_uart(void)
{
	PMAPKEYID = PMAPKEY;
	P1MAP6 = PM_UCA0TXD;  // Map UCA0TXD output to P1.6 
	PMAPKEYID = 0;

	P1DIR |= BIT6; // Set P1.6 as TX output
	P1SEL |= BIT6;

	UCA0CTL1 = UCSWRST | UCSSEL_2; // reset + SMCLK
	// The divider is 6500000 / 9600 = 677 = 0x2a5
	UCA0BR0 = 0xa5;
	UCA0BR1 = 2;
	UCA0CTL1 &= ~UCSWRST;
}

static inline void uart_send_char(unsigned char c)
{
	while (!(UCA0IFG&UCTXIFG)); // USCI_A0 TX buffer ready?
	UCA0TXBUF = c;              // TX -> RXed character
}

void uart_send_time_hex(unsigned val)
{
	int i;
	unsigned char time[4];
	unsigned char buff[6];
	unpack4nibbles(val, time);
	buff[0] =  't';
	buff[1] = '0' + time[3];
	buff[2] = '0' + time[2];
	buff[3] = '0' + time[1];
	buff[4] = '0' + time[0];
	buff[5] =  '\n';
	for (i = 0; i < 6; ++i) {
		uart_send_char(buff[i]);
	}
}
