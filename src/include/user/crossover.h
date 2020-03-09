/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2020 Google LLC. All rights reserved.
 *
 * Author: Sebastiano Carlucci <scarlucci@google.com>
 */

#ifndef __USER_CROSSOVER_H__
#define __USER_CROSSOVER_H__

#include <stdint.h>

#define CROSSOVER_MAX_SINKS 4

 /* crossover_configuration
  *     uint32_t channels_in_config
  *         This describes the number of channels in this Crossover config data. It
  *         can be different from PLATFORM_MAX_CHANNELS.
  *     uint32_t number_of_responses_defined
  *         0=no responses, 1=one response defined, 2=two responses defined, etc.
  *     uint32_t number_of_sinks <= 4
  *         1=passthrough, 2=2-way crossover, 3=3-way crossover, 4=4-way crossover.
  *     int32_t data[]
  *         Data consist of three  parts. First is the sink assign vector that
  *	    has length of 4 (the maximum number of sinks allowed). Then it has
  *	    the response assign vector of length channels_in_config.
  *	    The last part is the coefficients data.
  *
  *         uint32_t assign_sink[4]
  *             sink[0] = pipe_id_0, sink[1] = pipe_id_1, etc..
  *             Each entry will assign the i-th sink to the corresponding pipeline.
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
  *                  |              o---- LR4 LP2 -----> MID sink[2]
  *                  |              |
  *                  o--- LR4 HP0 --o
  *                                 |
  *                                 o---- LR4 HP2 -----> HIGH sink[3]
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
  *             E.g. {0, 0, 0, 0, -1, -1, -1, -1} would apply to channels 0-3 the
  *             same first defined response and leave channels 4-7 as passthrough.
  *          coefficient_data[]
  *             <1st Crossover Filter>
  *             <1st Low Pass LR4>
  *             int32_t coef_a2       Q2.30 format
  *             int32_t coef_a1       Q2.30 format
  *             int32_t coef_b2       Q2.30 format
  *             int32_t coef_b1       Q2.30 format
  *             int32_t coef_b0       Q2.30 format
  *             int32_t output_shift  number of shifts right, shift left is negative
  *             int32_t output_gain   Q2.14 format
  *             <2nd Low Pass LR4>
  *             ... There are 3 Low Pass LR4s and 3 High Pass LR4s ...
  *             <2nd Crossover Filter>
  *
  *         Note: A flat response biquad can be made with a section set to
  *         b0 = 1.0, gain = 1.0, and other parameters set to 0
  *         {0, 0, 0, 0, 1073741824, 0, 16484}
  */
struct sof_crossover_config {
        uint32_t size;
        uint32_t channels_in_config;
        uint32_t number_of_responses;
        uint32_t num_sinks;

        /* reserved */
        uint32_t reserved[4];

        /*
	 * assign_sink[CROSSOVER_MAX_SINKS] + 
         * assign_responses[channels_in_config] +
         * struct sof_crossover_config_data[number_of_responses]
         */
        int32_t data[];
} __attribute__((packed));

struct sof_crossover_config_data {
        /* reserved */
        uint32_t reserved[4];

        struct sof_crossover_config_lr4 lr4_coeffs[6];
};

struct sof_crossover_config_lr4 {
        int32_t a2; /* Q2.30 */
	int32_t a1; /* Q2.30 */
	int32_t b2; /* Q2.30 */
	int32_t b1; /* Q2.30 */
	int32_t b0; /* Q2.30 */
	int32_t output_shift; /* Number of right shifts */
	int32_t output_gain;  /* Q2.14 */
}


 #endif // __USER_CROSSOVER_H__
