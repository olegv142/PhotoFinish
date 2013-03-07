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
	char burst_bits;   // The log of the number of conversions in one run
	char detected_cnt; // The number of runs where detection takes place
	char detected_hi;  // The detection polarity (1-high pulse, -1-low pulse)
	int  detected_thr; // The signal threshold
	unsigned sample[2];// Samples collected during the last run for IR off/on
	int  signal;   // Signal
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

void phs_init(struct phs_ctx* ctx);
void phs_restart(struct phs_ctx* ctx);
void phs_run(struct phs_ctx* ctx);
