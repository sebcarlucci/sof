/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2020 Google LLC. All rights reserved.
 *
 * Author: Sebastiano Carlucci <scarlucci@google.com>
 */

#ifndef __USER_CROSSOVER_H__
#define __USER_CROSSOVER_H__

#include <stdint.h>
#include <user/eq.h>

/* Maximum Number of sinks allowed in config */
#define SOF_CROSSOVER_MAX_STREAMS 4

/* Maximum number allowed in configuration blob */
#define SOF_CROSSOVER_MAX_SIZE 1024

 /* crossover_configuration
  *     uint32_t number_of_sinks <= 4
  *         1=passthrough, n=n-way cossover.
  *     uint32_t assign_sinks[SOF_CROSSOVER_MAX_STREAMS]
  *         Mapping between sink positions and the sink's pipeline id. To assign
  *         output j to sink i, set: assign_sinks[j] = i. Refer to the diagrams
  *         below for more information on the sink outputs.
  *         TODO UPDATE 4way and 3way diagram if phase does not need to be adjusted.
  *         TODO Look at crossover_generic.c to see how the sinks should be routed.
  *
  *		4-way:
  *                                 o---- LR4 LP0 --> LOW assign_sink[0]
  *                                 |
  *                  o--- LR4 LP1 --o
  *                  |              |
  *                  |              o---- LR4 HP0 --> MID_LOW assign_sink[1]
  *         x(n) --- o
  *                  |              o---- LR4 LP2 --> MID_HIGH assign_sink[2]
  *                  |              |
  *                  o--- LR4 HP1 --o
  *                                 |
  *                                 o---- LR4 HP2 --> HIGH assign_sink[3]
  *
  *		3-way:
  *                                 o---- LR4 LP1 ---o
  *                                 |                |
  *                  o--- LR4 LP0 --o                +-> LOW assign_sink[0]
  *                  |              |                |
  *                  |              o---- LR4 HP1 ---o
  *         x(n) --- o
  *                  |              o---- LR4 LP2 -----> MID assign_sink[1]
  *                  |              |
  *                  o--- LR4 HP0 --o
  *                                 |
  *                                 o---- LR4 HP2 -----> HIGH assign_sink[2]
  *
  *		2-way:
  *                  o--- LR4 LP0 ---> LOW assign_sink[0]
  *                  |
  *         x(n) --- o
  *                  |
  *                  o--- LR4 HP0 ---> HIGH assign_sink[1]
  *
  *         struct sof_eq_iir_biquad_df2t coef[(num_sinks - 1)*2]
  *	    	The coefficients data for the LR4s. Depending on many
  *	    	sinks are set, the number entries of this field can vary.
  *	    	Each entry of the array defines the coefficients for one biquad
  *	    	of the LR4 filter. The order the coefficients are assigned is:
  *	    	[LR4 LP0, LR4 HP0, LR4 LP1, LR4 HP1, LR4 LP2, LR4 HP2].
  *	    	coef[0] = coefficients of LR4 LP0...
  *		coef[1] = coefficients of LR4 HP0...
  *		...
  *		
  *		Since an LR4 is two biquads in series with same coefficients,
  *		the config takes the coefficients for one biquad and
  *		assigns it to both biquads of the LR4.
  *
  *	     	<1st Low Pass LR4>
  *	     	int32_t coef_a2       Q2.30 format
  *	     	int32_t coef_a1       Q2.30 format
  *	     	int32_t coef_b2       Q2.30 format
  *	     	int32_t coef_b1       Q2.30 format
  *	     	int32_t coef_b0       Q2.30 format
  *	     	int32_t output_shift  number of right shift (nve for left)
  *	     	int32_t output_gain   Q2.14 format
  *	     	<1nd High Pass LR4>
  *	     	...
  *	     	<2nd Low Pass LR4>
  *	     	<2nd High Pass LR4>
  *	     	...
  *	     	... At most 3 Low Pass LR4s and 3 High Pass LR4s ...
  *
  */
struct sof_crossover_config {
	uint32_t size;
	uint32_t num_sinks;

	/* reserved */
	uint32_t reserved[4];	

	uint32_t assign_sink[SOF_CROSSOVER_MAX_STREAMS];
	struct sof_eq_iir_biquad_df2t coef[];
};

/* TODO this is not useful, revisit this */
#define SOF_CROSSOVER_LR4_COEF_SIZE  \
	(sizeof(struct sof_eq_iir_biquad_df2t) / sizeof(int32_t))

/* Returns the number of LR4 for the given number of sinks */
#define SOF_CROSSOVER_NUM_LR4(sinks) (((sinks) - 1) * 2)

#define SOF_CROSSOVER_COEF_SIZE(num_sinks) \
	(SOF_CROSSOVER_LR4_COEF_SIZE * SOF_CROSSOVER_NUM_COEF(num_sinks))

#endif // __USER_CROSSOVER_H__
