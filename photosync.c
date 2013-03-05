#include "io430.h"
#include "common.h"
#include "photosync.h"

#define DET_LO /* Detect signal going low */
#define DET_HI /* Detect signal going high */

#define SHT_MAX 12
#define HI_THR 3500
#define LO_THR 1700
#define OVERLOAD 4095

#define DET_MIN_CNT 2
#define DET_MIN_THR 3
#define DET_SNR_THR 6
#define DET_SIG_FRACTION 4

//#define ADC_INP_CHANEL ADC12INCH_10 /* Temp sensor for testing */
#define ADC_INP_CHANEL ADC12INCH_0
#define ADC_ZER_CHANEL ADC12INCH_1
#define ADC_REF ADC12SREF_1 // V(R+) = VREF+ and V(R-) = AVSS
#define ADC_CLR_SHT 2

static inline void phs_adc_init(unsigned sht, unsigned chan)
{
	ADC12CTL0  = ADC12ON|(sht << 8);
	ADC12CTL1  = ADC12SSEL_2|ADC12SHP; // MCLK
	ADC12MCTL0 = chan|ADC_REF;
	ADC12CTL0 |= ADC12ENC;
}

static inline void phs_adc_off()
{
	ADC12CTL0 &= ~(ADC12ON|ADC12ENC);
}

/*
 * The photosensor design is simple/stupid. There are only 2
 * external components - IR LED and photodiode (PHD). The
 * PHD is biased by reference output pin connected to its cathode.
 * The anode is connected to A0 input (P2.0). Normally the MCU
 * is providing low potential to this pin. Only during
 * ADC conversion cycle the pin is reconfigured as analog
 * input so the photocurrent starts charging the sample-hold
 * capacitor (~25pF) as well as the PHD itself (~100pF).
 * So we can change the sensitivity by just changing the
 * sample-hold time.
 */

static inline int phs_measure(unsigned sht, unsigned chan, char inp_en)
{
	// Disable interrupts for predictable timing
	__disable_interrupt();
	// Initialize ADC
	phs_adc_init(sht, chan);
	if (inp_en) {
		P2DIR &= ~BIT0;
		P2SEL |= BIT0;
	}
	// Start conversion
	ADC12CTL0 |= ADC12SC;
	// The conversion is started so we can enable interrupts
	__enable_interrupt();
	// Wait conversion completion
	while (!(ADC12IFG & ADC12IFG0))
		__no_operation();
	if (inp_en) {
		P2DIR |= BIT0;
		P2SEL &= ~BIT0;
	}
	// Turn off ADC
	phs_adc_off();
	// Return result
	return ADC12MEM0;
}

static int phs_get_sample(unsigned sht, char ir_on)
{
	int sample;
	if (ir_on) P1OUT |= IR_BITS;
	// Dummy measurement to discharge sample-hold capacitor
	phs_measure(ADC_CLR_SHT, ADC_ZER_CHANEL, 0);
	sample = phs_measure(sht,  ADC_INP_CHANEL, 1);
	if (ir_on) P1OUT &= ~IR_BITS;
	return sample;
}

void phs_acquire(struct phs_ctx* ctx)
{
	/* Run 2 measurement cycles with IR off/on */
	ctx->sample[0] = phs_get_sample(ctx->sht, 0);
	ctx->sample[1] = phs_get_sample(ctx->sht, 1);
}

static inline int abs(int v)
{
	return v >= 0 ? v : -v;
}

static void phs_detect(struct phs_ctx* ctx, int signal);

void phs_run(struct phs_ctx* ctx)
{
	int v0, v1, signal;
	phs_acquire(ctx);
	v0 = ctx->sample[0];
	v1 = ctx->sample[1];
	signal = v1 - v0;

	if (ctx->detection && ctx->ready)
		phs_detect(ctx, signal);

	if (!ctx->detected_cnt && ctx->autoscale) {
		// Auto scaling
		if (ctx->sht > 0 && v1 > HI_THR) {
			--ctx->sht;
			phs_restart(ctx);
			return;
		}
		if (ctx->sht < SHT_MAX && v1 < LO_THR) {
			++ctx->sht;
			phs_restart(ctx);
			return;
		}
	}

	ctx->overload = (v0 >= OVERLOAD || v1 >= OVERLOAD);
	aver_arr_put(ctx->asample, 2, v1);
	aver_arr_put(ctx->asignal, 2, signal);
	if (ctx->asignal[1].ready)
		aver_arr_put(ctx->anoise, 2, DET_SNR_THR * abs(signal - aver_value(&ctx->asignal[1])));
	ctx->ready = ctx->anoise[1].ready;
}

static void phs_detect(struct phs_ctx* ctx, int signal)
{
	if (ctx->detected_cnt) {
#ifdef DET_LO
		if (!ctx->detected_hi) {
			if (signal < ctx->detected_thr)
				++ctx->detected_cnt;
			else
				ctx->detected_cnt = 0;
		}
#endif
#ifdef DET_HI
		if (ctx->detected_hi) {
			if (signal > ctx->detected_thr)
				++ctx->detected_cnt;
			else
				ctx->detected_cnt = 0;
		}
#endif
	} else {
		int thr = DET_MIN_THR;
		int aver_signal = aver_value(&ctx->asignal[1]);
		int noise_thr   = aver_value(&ctx->anoise[1]);
		int signal_thr  = aver_signal / DET_SIG_FRACTION;
		if (thr < noise_thr)
			thr = noise_thr;
		if (thr < signal_thr)
			thr = signal_thr;
#ifdef DET_LO
		if (signal < aver_signal - thr) {
			ctx->detected_cnt = 1;
			ctx->detected_hi  = 0;
			ctx->detected_thr = aver_signal - thr;
		}
#endif
#ifdef DET_HI
		if (signal > aver_signal + thr) {
			ctx->detected_cnt = 1;
			ctx->detected_hi  = 1;
			ctx->detected_thr = aver_signal + thr;
		}
#endif
	}
	if (ctx->detected_cnt >= DET_MIN_CNT) {
		ctx->detection = 0;
		ctx->detected = 1;
	}
}
