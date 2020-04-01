/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2020 Google LLC. All rights reserved.
 *
 * Author: Sebastiano Carlucci <scarlucci@google.com>
 */

#ifndef __USER_CROSSOVER_H__
#define __USER_CROSSOVER_H__

#include <stdint.h>

/* Maximum Number of sinks allowed in config */
#define SOF_CROSSOVER_MAX_SINKS 4

/* Maximum number allowed in configuration blob */
#define SOF_CROSSOVER_MAX_SIZE 1024

/* A config blob can define at most 8 responses */
#define SOF_CROSSOVER_MAX_RESPONSES 8

 /* crossover_configuration
  *     uint32_t channels_in_config
  *         This describes the number of channels in this Crossover config data.
  *         It can be different from PLATFORM_MAX_CHANNELS.
  *     uint32_t number_of_responses_defined
  *         0=no responses, 1=one response defined, 2=two responses defined, etc
  *     uint32_t number_of_sinks <= 4
  *         1=passthrough, n=n-way cossover.
  *     int32_t data[]
  *         Data consist of three  parts. First is the sink assign vector that
  *	    has length of 4 (the maximum number of sinks allowed). Then it has
  *	    the response assign vector of length channels_in_config.
  *	    The last part is the coefficients data.
  *
  *         uint32_t assign_sink[4]
  *             sink[0] = pipe_id_0, sink[1] = pipe_id_1, etc..
  *             Each entry will assign the i-th sink to the corresponding
  *             pipeline.
  *		4-way:
  *                                 o---- LR4 LP1 --> LOW sink[0]
  *                                 |
  *                  o--- LR4 LP0 --o
  *                  |              |
  *                  |              o---- LR4 HP1 --> MID_LOW sink[1]
  *         x(n) --- o
  *                  |              o---- LR4 LP2 --> MID_HIGH sink[2]
  *                  |              |
  *                  o--- LR4 HP0 --o
  *                                 |
  *                                 o---- LR4 HP2 --> HIGH sink[3]
  *
  *		3-way:
  *                                 o---- LR4 LP1 ---o
  *                                 |                |
  *                  o--- LR4 LP0 --o                +-> LOW sink[0]
  *                  |              |                |
  *                  |              o---- LR4 HP1 ---o
  *         x(n) --- o
  *                  |              o---- LR4 LP2 -----> MID sink[1]
  *                  |              |
  *                  o--- LR4 HP0 --o
  *                                 |
  *                                 o---- LR4 HP2 -----> HIGH sink[2]
  *
  *		2-way:
  *                  o--- LR4 LP0 ---> LOW sink[0]
  *                  |
  *         x(n) --- o
  *                  |
  *                  o--- LR4 HP0 ---> HIGH sink[1]
  *
  *         uint32_t assign_response[channels_in_config]
  *             -1 = not defined, 0 = use first response, 1 = use 2nd, etc.
  *             E.g. {0, 0, 0, 0, -1, -1, -1, -1} would apply to channels 0-3
  *             the same first defined response and leave channels 4-7
  *             as passthrough.
  *
  *          struct sof_crossover_config_coef coef[]
  *             <1st Crossover Filter>
  *	             <1st Low Pass LR4>
  *	             int32_t coef_a2       Q2.30 format
  *	             int32_t coef_a1       Q2.30 format
  *	             int32_t coef_b2       Q2.30 format
  *	             int32_t coef_b1       Q2.30 format
  *	             int32_t coef_b0       Q2.30 format
  *	             int32_t output_shift  number of right shift (nve for left)
  *	             int32_t output_gain   Q2.14 format
  *	             <1nd High Pass LR4>
  *	             ...
  *	             <2nd Low Pass LR4>
  *	             <2nd High Pass LR4>
  *	             ...
  *	             ... In total 3 Low Pass LR4s and 3 High Pass LR4s ...
  *	    <2nd Crossover Filter>
  *
  *         Note: A flat response biquad can be made with a section set to
  *         b0 = 1.0, gain = 1.0, and other parameters set to 0
  *         {0, 0, 0, 0, 1073741824, 0, 16484}
  */
struct sof_crossover_config_lr4 {
	int32_t a2; /* Q2.30 */
	int32_t a1; /* Q2.30 */
	int32_t b2; /* Q2.30 */
	int32_t b1; /* Q2.30 */
	int32_t b0; /* Q2.30 */
	int32_t output_shift; /* Number of right shifts */
	int32_t output_gain;  /* Q2.14 */
};

struct sof_crossover_config {
	uint32_t size;                 /* size of data array */
	uint32_t channels_in_config;
	uint32_t number_of_responses;
	uint32_t num_sinks;

	/* reserved */
	uint32_t reserved[4];

	/*
	 * assign_sink[num_sinks] +
	 * assign_responses[channels_in_config] +
	 * struct sof_crossover_config_coef[number_of_responses]
	 */
	int32_t data[];
};

#define SOF_CROSSOVER_LR4_COEF_SIZE  \
	(sizeof(struct sof_crossover_config_lr4) / sizeof(int32_t))

#define SOF_CROSSOVER_NUM_COEF(sinks) (((sinks) - 1) * 2)

#define SOF_CROSSOVER_COEF_SIZE(num_sinks) \
	(SOF_CROSSOVER_LR4_COEF_SIZE * SOF_CROSSOVER_NUM_COEF(num_sinks))

#endif // __USER_CROSSOVER_H__
