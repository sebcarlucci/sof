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
#include <user/crossover.h>

struct comp_buffer;
struct comp_dev;

#define CROSSOVER_MAX_STREAMS 4
#define CROSSOVER_BIQUAD_COEF_SIZE (sizeof(int32_t) * 7)

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
 * Refer to include/user/crossover.h for diagrams of 2-way and 3-way crossovers
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

struct lr4_state {
	int32_t *coef;
	int64_t *delay;
} lr4_state;

/**
 * \brief Stores the state of one channel of the Crossover filter
 */
struct crossover_state {
	/* Store the state for each LR4 filter. */
	lr4_state lowpass[3];
	lr4_state highpass[3];
};

typedef void (*crossover_func)(const struct comp_dev *dev,
			       const struct comp_buffer *source,
			       struct comp_buffer *sinks[],
			       int32_t num_sinks,
			       uint32_t frames);

/* Crossover component private data */
struct comp_data {
	/**< filter state */
	struct crossover_state state[PLATFORM_MAX_CHANNELS];
	struct sof_crossover_config *config;      /**< pointer to setup blob */
	struct sof_crossover_config *config_new;  /**< pointer to new setup */
	enum sof_ipc_frame source_format;         /**< source frame format */
	enum sof_ipc_frame sink_format;           /**< sink frame format */
	crossover_func crossover_func;            /**< processing function */
	int32_t num_sinks;			  /**< number of outputs */
	uint32_t sinks[CROSSOVER_MAX_STREAMS];    /**< sink assignemnts */
};

struct crossover_func_map {
	enum sof_ipc_frame frame_fmt;
	crossover_func crossover_proc_func;
};

extern const struct crossover_func_map crossover_fnmap[];
extern const struct crossover_func_map crossover_fnmap_pass[];
extern const size_t crossover_fncount;

/**
 * \brief Retrieves Crossover processing function.
 */
static inline crossover_func crossover_find_func(enum sof_ipc_frame src_fmt)
{
	int i;

	/* Find suitable processing function from map */
	for (i = 0; i < crossover_fncount; i++)
		if (src_fmt == crossover_fnmap[i].frame_fmt)
			return crossover_fnmap[i].crossover_proc_func;

	return NULL;
}

/**
 * \brief Retrieves Crossover passthrough functions.
 */
static inline crossover_func
	crossover_find_func_pass(enum sof_ipc_frame src_fmt)
{
	int i;

	/* Find suitable processing function from map */
	for (i = 0; i < crossover_fncount; i++)
		if (src_fmt == crossover_fnmap_pass[i].frame_fmt)
			return crossover_fnmap_pass[i].crossover_proc_func;

	return NULL;
}

#endif //  __SOF_AUDIO_CROSSOVER_CROSSOVER_H__
