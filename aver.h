#pragma once

/* Sliding window average */

#define AVER_WND_LEN 16

struct aver_ctx {
	int	val[AVER_WND_LEN];
	char	next;
	char	ready;
	long 	total;
};

/* Reset state to empty */
static inline void aver_reset(struct aver_ctx* ctx)
{
	ctx->next  = 0;
	ctx->ready = 0;
	ctx->total = 0;
}

/* Returns 1 right after reset */
static inline int aver_empty(struct aver_ctx const* ctx)
{
	return !ctx->ready && !ctx->next;
}

/* Put new value */
static inline void aver_put(struct aver_ctx* ctx, int val)
{
	ctx->total += val;
	if (ctx->ready)
		ctx->total -= ctx->val[ctx->next];
	ctx->val[ctx->next] = val;
	if (++ctx->next >= AVER_WND_LEN) {
		ctx->next  = 0;
		ctx->ready = 1;
	}
}

/* Returns average value. Should be called only if ctx->ready != 0 */
static inline int aver_value(struct aver_ctx const* ctx)
{
	return ctx->total / AVER_WND_LEN;
}

/* Returns average value multiplied by given factor. */
static inline int aver_value_scaled(struct aver_ctx const* ctx, int scale)
{
	return (ctx->total * scale) / AVER_WND_LEN;
}

/*
 * Combining more than one plain windows gives longer window approximation
 */

static inline void aver_arr_reset(struct aver_ctx* ctx, int n)
{
	for (; n; --n, ++ctx)
		aver_reset(ctx);
}

static inline void aver_arr_put(struct aver_ctx* ctx, int n, int val)
{
	for (; n; --n, ++ctx) {
		aver_put(ctx, val);
		if (ctx->next)
			return;
		val = aver_value(ctx);
	}
}
