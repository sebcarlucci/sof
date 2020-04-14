// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2017 Intel Corporation. All rights reserved.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>
//         Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>

#include <sof/audio/eq_iir/iir.h>
#include <sof/audio/format.h>
#include <user/eq.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#if IIR_GENERIC

/*
 * Direct form II transposed second order filter block (biquad)
 *
 *              +----+                         +---+    +-------+
 * X(z) ---o--->| b0 |---> + -------------o--->| g |--->| shift |---> Y(z)
 *         |    +----+     ^              |    +---+    +-------+
 *         |               |              |
 *         |            +------+          |
 *         |            | z^-1 |          |
 *         |            +------+          |
 *         |               ^              |
 *         |    +----+     |     +----+   |
 *         o--->| b1 |---> + <---| a1 |---o
 *         |    +----+     ^     +----+   |
 *         |               |              |
 *         |            +------+          |
 *         |            | z^-1 |          |
 *         |            +------+          |
 *         |               ^              |
 *         |    +----+     |     +----+   |
 *         o--->| b2 |---> + <---| a2 |---+
 *              +----+           +----+
 *
 */

/* Series DF2T IIR */

/* 32 bit data, 32 bit coefficients and 64 bit state variables */

int32_t iir_process_biquad(int32_t in, int32_t *coef, int64_t *delay)
{
	int32_t tmp;
	int64_t acc;

	/* Compute output: Delay is Q3.61
	 * Q2.30 x Q1.31 -> Q3.61
	 * Shift Q3.61 to Q3.31 with rounding
	 */
	acc = ((int64_t)coef[4]) * in + delay[0];
	tmp = (int32_t)Q_SHIFT_RND(acc, 61, 31);

	/* Compute first delay */
	acc = delay[1];
	acc += ((int64_t)coef[3]) * in; /* Coef  b1 */
	acc += ((int64_t)coef[1]) * tmp; /* Coef a1 */
	delay[0] = acc;

	/* Compute second delay */
	acc = ((int64_t)coef[2]) * in; /* Coef  b2 */
	acc += ((int64_t)coef[0]) * tmp; /* Coef a2 */
	delay[1] = acc;

	/* Apply gain Q2.14 x Q1.31 -> Q3.45 */
	acc = ((int64_t)coef[6]) * tmp; /* Gain */

	/* Apply biquad output shift right parameter
	 * simultaneously with Q3.45 to Q3.31 conversion. Then
	 * saturate to 32 bits Q1.31 and prepare for next
	 * biquad.
	 */
	acc = Q_SHIFT_RND(acc, 45 + coef[5], 31);
	return sat_int32(acc);
}

int32_t iir_df2t(struct iir_state_df2t *iir, int32_t x)
{
	int32_t in;
	int32_t out = 0;
	int i;
	int j;
	int d = 0; /* Index to delays */
	int c = 0; /* Index to coefficient a2 */

	/* Bypass is set with number of biquads set to zero. */
	if (!iir->biquads)
		return x;

	/* Coefficients order in coef[] is {a2, a1, b2, b1, b0, shift, gain} */
	in = x;
	for (j = 0; j < iir->biquads; j += iir->biquads_in_series) {
		for (i = 0; i < iir->biquads_in_series; i++) {
			in = iir_process_biquad(in, &iir->coef[c],
						&iir->delay[d]);

			/* Proceed to next biquad coefficients and delay
			 * lines.
			 */
			c += SOF_EQ_IIR_NBIQUAD_DF2T;
			d += IIR_DF2T_NUM_DELAYS;
		}
		/* Output of previous section is in variable in */
		out = sat_int32((int64_t)out + in);
	}
	return out;
}

#endif

