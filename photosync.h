#pragma once

/* Synchronous photo-detector driver */

#include "aver.h"

struct phs_ctx {
	char autoscale;    // Autoscale enabled
	char detection;    // Detection enabled
	char detected;     // Detected flag
	char ready;        // Ready flag
	char overload;     // Overload by too bright ambient light
	char sht;          // Current sample-hold time setting
	char detected_cnt; // The number of runs where detection takes place
	char detected_hi;  // The detection polarity (1-high pulse, -1-low pulse)
	int  detected_thr; // The signal threshold
	int  sample[2];    // Samples collected during the last run for IR off/on
	// Sliding average buffers collecting samples with IR on, synch signal and its deviation
	struct aver_ctx asample[2];
	struct aver_ctx asignal[2];
	struct aver_ctx	anoise[2];
};

static inline void phs_set_mode(struct phs_ctx* ctx, char autoscale, char detection)
{
	ctx->autoscale = autoscale;
	ctx->detection = detection;
	ctx->detected = ctx->detected_cnt = 0;
}

static inline void phs_restart(struct phs_ctx* ctx)
{
	aver_arr_reset(ctx->asample, 2);
	aver_arr_reset(ctx->asignal, 2);
	aver_arr_reset(ctx->anoise, 2);
	ctx->detected = ctx->detected_cnt = 0;
	ctx->ready = 0;
}

//#define ADC_INP_CHANEL ADC12INCH_10 /* Temp sensor for testing */
#define ADC_INP_CHANEL ADC12INCH_0
#define ADC_ZER_CHANEL ADC12INCH_1
#define ADC_REF ADC12SREF_1 // V(R+) = VREF+ and V(R-) = AVSS
#define ADC_CLR_SHT 2

static inline void phs_init_adc(struct phs_ctx* ctx)
{
	ADC12CTL0  = ADC12ON|ADC12MSC|((int)ctx->sht << 8)|(ADC_CLR_SHT << 12);
	ADC12CTL1  = ADC12SSEL_2|ADC12SHP|ADC12CSTARTADD_7|ADC12CONSEQ_1; // MCLK, conversion sequence starting from addr 7
	ADC12MCTL7 = ADC_INP_CHANEL|ADC_REF;
	ADC12MCTL8 = ADC_ZER_CHANEL|ADC_REF|ADC12EOS;
	ADC12CTL0 |= ADC12ENC;
}

static inline void phs_init(struct phs_ctx* ctx)
{
	// Configure pins
	P2OUT &= ~(BIT0|BIT1);
	P2DIR |= BIT0|BIT1;
	P2SEL |= BIT5; // P2.5 is refereference output
	P1DS |= IR_BITS; // Max drive strength for IR LEDs
	// Configure reference: 1.5V, output enable
	REFCTL0 = REFMSTR|REFON|REFOUT;
	ctx->sht = 0;
	phs_init_adc(ctx);
}

void phs_run(struct phs_ctx* ctx);
