#include "io430.h"
#include "common.h"
#include "photosync.h"

#define DET_LO /* Detect signal going low */
#define DET_HI /* Detect signal going high */

#define SHT_MAX 12
#define ADC_CLR_SHT 2
#define HI_THR 3500
#define LO_THR 1700
#define OVERLOAD 4095

#define DET_MIN_CNT 2
#define DET_MIN_THR 3
#define DET_SNR_THR 6
#define DET_SIG_FRACTION 4

#define ADC_INP_CHANEL ADC12INCH_0
//#define ADC_INP_CHANEL ADC12INCH_10 /* Temp sensor for testing */
#define ADC_REF ADC12SREF_1 // V(R+) = VREF+ and V(R-) = AVSS

#define BURST_BITS(sht) ((SHT_MAX - sht) / 3)

void phs_init(struct phs_ctx* ctx)
{
	// Configure pins
	P2OUT &= ~BIT0;     // P2.0 is connected to the photo-diode (anode)
	P2DIR |= BIT0;
	P2SEL |= BIT0|BIT5; // P2.5 is refereference output connected to the photo-diode (cathode)
	P1DS |= IR_BITS;    // Max drive strength for IR LEDs
	// Configure reference: 1.5V, output enable
	REFCTL0 = REFMSTR|REFON|REFOUT;
	// Use MCLK for ADC (just to be in sync with code execution)
	ADC12CTL1  = ADC12SSEL_2|ADC12SHP;
	// Configure input channel
	ADC12MCTL0 = ADC_INP_CHANEL|ADC_REF;
	// Start from the less sensitive range
	ctx->sht = 0;
	phs_restart(ctx);
}

void phs_restart(struct phs_ctx* ctx)
{
	aver_arr_reset(ctx->asample, 2);
	aver_arr_reset(ctx->asignal, 2);
	aver_arr_reset(ctx->anoise, 2);
	ctx->detected = ctx->detected_cnt = 0;
	ctx->ready = 0;
	ctx->burst_bits = BURST_BITS(ctx->sht);
}

static inline void phs_adc_on(unsigned sht)
{
	ADC12CTL0  = ADC12ON|(sht << 8);
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
 * capacitor (~25pF) as well as the PHD itself (~10pF).
 * So we can change the sensitivity by just changing the
 * sample-hold time.
 */

static inline int phs_conversion(unsigned sht, char inp_en)
{
	// Disable interrupts for predictable timing
	__disable_interrupt();
	// Initialize ADC
	phs_adc_on(sht);
	if (inp_en) P2DIR &= ~BIT0;
	// Start conversion
	ADC12CTL0 |= ADC12SC;
	// The conversion is started so we can enable interrupts
	__enable_interrupt();
	// Wait conversion completion
	while (!(ADC12IFG & ADC12IFG0))
		__no_operation();
	if (inp_en) P2DIR |= BIT0;
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
	phs_conversion(ADC_CLR_SHT, 0);
	sample = phs_conversion(sht,  1);
	if (ir_on) P1OUT &= ~IR_BITS;
	return sample;
}

static void phs_acquire(struct phs_ctx* ctx)
{
	int loops;
	ctx->sample[0] = ctx->sample[1] = 0;
	// Run up to 16 cycles
	for (loops = 1 << ctx->burst_bits; loops; --loops) {
		/* Run 2 measurement cycles with IR off/on */
		ctx->sample[0] += phs_get_sample(ctx->sht, 0);
		ctx->sample[1] += phs_get_sample(ctx->sht, 1);
	}
	ctx->signal = ctx->sample[1] - ctx->sample[0];
	ctx->sample[0] >>= ctx->burst_bits;
	ctx->sample[1] >>= ctx->burst_bits;
}

static inline int abs(int v)
{
	return v >= 0 ? v : -v;
}

static void phs_detect(struct phs_ctx* ctx)
{
	if (ctx->detected_cnt) {
#ifdef DET_LO
		if (!ctx->detected_hi) {
			if (ctx->signal < ctx->detected_thr)
				++ctx->detected_cnt;
			else
				ctx->detected_cnt = 0;
		}
#endif
#ifdef DET_HI
		if (ctx->detected_hi) {
			if (ctx->signal > ctx->detected_thr)
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
		if (ctx->signal < aver_signal - thr) {
			ctx->detected_cnt = 1;
			ctx->detected_hi  = 0;
			ctx->detected_thr = aver_signal - thr;
		}
#endif
#ifdef DET_HI
		if (ctx->signal > aver_signal + thr) {
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

void phs_process(struct phs_ctx* ctx)
{
	if (ctx->detection && ctx->ready)
		phs_detect(ctx);

	if (!ctx->detected_cnt && ctx->autoscale) {
		// Auto scaling
		if (ctx->sht > 0 && ctx->sample[1] > HI_THR) {
			--ctx->sht;
			phs_restart(ctx);
			return;
		}
		if (ctx->sht < SHT_MAX && ctx->sample[1] < LO_THR) {
			++ctx->sht;
			phs_restart(ctx);
			return;
		}
	}

	ctx->overload = (ctx->sample[0] >= OVERLOAD || ctx->sample[1] >= OVERLOAD);
	aver_arr_put(ctx->asample, 2, ctx->sample[1]);
	aver_arr_put(ctx->asignal, 2, ctx->signal);
	if (ctx->asignal[1].ready)
		aver_arr_put(ctx->anoise, 2, DET_SNR_THR * abs(ctx->signal - aver_value(&ctx->asignal[1])));
	ctx->ready = ctx->anoise[1].ready;
}

void phs_run(struct phs_ctx* ctx)
{
	phs_acquire(ctx);
	phs_process(ctx);
}
