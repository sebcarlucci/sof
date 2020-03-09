/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2020 Google LLC. All rights reserved.
 *
 * Author: Sebastiano Carlucci <scarlucci@google.com>
 */

 #ifndef __SOF_AUDIO_CROSSOVER_CROSSOVER_H__
 #define __SOF_AUDIO_CROSSOVER_CROSSOVER_H__

#include <stdint.h>
#include <sof/platform.h>
#include <sof/audio/eq_iir/iir.h>

struct audio_stream;
struct comp_dev;

#define CROSSOVER_MAX_STREAMS 4

/**
* The Crossover filter will have from 2 to 4 outputs.
* Diagram of a 4-way Crossover filter (6 LR4 Filters).
*
*                             o---- LR4 LO-PASS --> y1(n)
*                             |
*          o--- LR4 LO-PASS --o
*          |                  |
*          |                  o--- LR4 HI-PASS --> y2(n)
* x(n) --- o
*          |                  o--- LR4 LO-PASS --> y3(n)
*          |                  |
*          o--- LR4 HI-PASS --o
*                             |
*                             o--- LR4 HI-PASS --> y4(n)
*
* The low and high pass LR4 filters have opposite phase responses, causing
* the intermediary outputs to be out of phase by 180 degrees.
* For 2-way and 3-way, the phases of the signals need to be synchronized.
*
* Each LR4 is made of two butterworth filters in series with the same params.
*
* x(n) --> BIQUAD --> z(n) --> BIQUAD --> y(n)
*
* In total, we keep track of the state of at most 6 IIRs each made of two
* biquads in series.
*
*/

typedef struct iir_state_df2t lr4_state;

/**
* \brief Stores the state of one channel of the Crossover filter
*/
struct crossover_state {
        /* Store the state for each LR4 filter. */
        lr4_state lowpass[3];
        lr4_state highpass[3];
};

typedef void (*crossover_func)(const struct comp_dev *dev,
                               const struct audio_stream *source,
                               struct audio_stream *sinks[],
                               int32_t num_sinks,
                               uint32_t frames);

/* Crossover component private data */
struct comp_data {
        struct crossover_state state[PLATFORM_MAX_CHANNELS]; /**< filter state */
        struct sof_crossover_config *config;      /**< pointer to setup blob */
        struct sof_crossover_config *config_new;  /**< pointer to new setup */
        enum sof_ipc_frame source_format;         /**< source frame format */
        enum sof_ipc_frame sink_format;           /**< sink frame format */
        crossover_func crossover_func;            /**< processing function */
        int32_t num_sinks;
};


struct crossover_func_map {
        enum sof_ipc_frame frame_fmt;
        crossover_func crossover_proc_func;
};

extern const struct crossover_func_map crossover_fnmap[];
extern const size_t crossover_fncount;

/**
 * \brief Retrieives Crossover processing function.
 * \param[in,out] dev DC Blocking Filter base component device.
 */
static inline crossover_func crossover_find_func(enum sof_ipc_frame src_fmt)
{
	int i;

	/* Find suitable processing function from map */
	for (i = 0; i < crossover_fncount; i++) {
		if (src_fmt == crossover_fnmap[i].frame_fmt)
			return crossover_fnmap[i].crossover_proc_func;
	}

	return NULL;
}


 #endif //  __SOF_AUDIO_CROSSOVER_CROSSOVER_H__
