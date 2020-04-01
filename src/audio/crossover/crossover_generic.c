// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020 Google LLC. All rights reserved.
//
// Author: Sebastiano Carlucci <scarlucci@google.com>

#include <stdint.h>
#include <sof/audio/format.h>
#include <sof/audio/eq_iir/iir.h>
#include <sof/audio/component.h>
#include <sof/audio/format.h>
#include <sof/audio/crossover/crossover.h>

// static void crossover_generic(struct crossover_state *state,
//                               int32_t x, int32_t y[], int32_t num_sinks)
// {
//         /* PASS THROUGH FOR NOW */
//         for (int i = 0; i < num_sinks; i++) {
//                 y[i] = x;
//         }
// }

/*
 * \brief Runs input in through the LR4 filter and returns it's output.
 */
static int32_t crossover_generic_process_lr4(int32_t in, lr4_state *lr4)
{
	int32_t z;

	/* Cascade two biquads with same coefficients in series */
	z = iir_process_biquad(in, lr4->coef, lr4->delay);
	return iir_process_biquad(z, lr4->coef, &lr4->delay[2]);
}

/*
 * \brief Splits x into two based on the coefficients set in the lp
 * and hp filters. The output of the lp is in y1, the output of
 * the hp is in y2.
 *
 * As a side effect, this function mutates the delay values of both
 * filters.
 */
static void crossover_generic_lr4_split(lr4_state *lp, lr4_state *hp,
					int32_t x, int *y1, int *y2)
{
	int32_t tmp;

	tmp = crossover_generic_process_lr4(x, lp);
	*y1 = sat_int16(Q_SHIFT_RND(tmp, 31, 15));

	tmp = crossover_generic_process_lr4(x, hp);
	*y2 = sat_int16(Q_SHIFT_RND(tmp, 31, 15));
}

static void crossover_s16_default_pass(const struct comp_dev *dev,
				       const struct comp_buffer *source,
				       struct comp_buffer *sinks[],
				       int32_t num_sinks,
				       uint32_t frames)
{
	const struct audio_stream *source_stream = &source->stream;
	int16_t *x;
	int16_t *y;
	int i;
	int j;
	int n = source_stream->channels * frames;

	for (i = 0; i < n; i++) {
		x = audio_stream_read_frag_s16(source_stream, i);
		for (j = 0; j < num_sinks; j++) {
			y = audio_stream_read_frag_s16((&sinks[j]->stream), i);
			*y = *x;
		}
	}
}

static void crossover_s16_default(const struct comp_dev *dev,
				  const struct comp_buffer *source,
				  struct comp_buffer *sinks[],
				  int32_t num_sinks,
				  uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct crossover_state *state = &cd->state[0];
	const struct audio_stream *source_stream = &source->stream;
	int16_t *x;
	int16_t *y1, *y2;
	int32_t z1, z2;
	int32_t tmp;
	int idx = 0;
	int i;
	int nch = source_stream->channels;

	/* [WIP] Right now it reads a sample from the first channel (input)
	 * and stores the two outputs on the L/R channels on the first
	 * available sink.
	 *
	 * Only supports two outputs for testing.
	 */
	for (i = 0; i < frames; i++) {
		x = audio_stream_read_frag_s16(source_stream,
					       idx);
		y1 = audio_stream_read_frag_s16((&sinks[0]->stream),
						idx);

		y2 = audio_stream_read_frag_s16((&sinks[0]->stream),
						idx + 1);

		crossover_generic_lr4_split(&state->lowpass[0],
					    &state->highpass[0],
					    *x << 16, (int *)&z1, (int *)&z2);

		// TESTING FOR NOW
		switch (nch) {
		case 2:
			*y1 = z1;
			*y2 = z2;
			break;
		case 4:
			crossover_generic_lr4_split(&state->lowpass[1],
						    &state->highpass[1],
						    z1 << 16, (int *)y1, &tmp);

			crossover_generic_lr4_split(&state->lowpass[2],
						    &state->highpass[2],
						    z2 << 16, &tmp, (int *)y2);
			break;
		default:
			comp_err(dev, "nch %i  not supported", nch);
			return;
		}
		idx += nch;
	}
}

const struct crossover_func_map crossover_fnmap[] = {
/* { SOURCE_FORMAT , PROCESSING FUNCTION } */
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, crossover_s16_default },
#endif /* CONFIG_FORMAT_S16LE */
// #if CONFIG_FORMAT_S24LE
// { SOF_IPC_FRAME_S24_4LE, crossover_s24_default },
// #endif /* CONFIG_FORMAT_S24LE */
// #if CONFIG_FORMAT_S32LE
// { SOF_IPC_FRAME_S32_LE, crossover_s32_default },
// #endif /* CONFIG_FORMAT_S32LE */
};

const struct crossover_func_map crossover_fnmap_pass[] = {
/* { SOURCE_FORMAT , PROCESSING FUNCTION } */
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, crossover_s16_default_pass },
#endif /* CONFIG_FORMAT_S16LE */

};

const size_t crossover_fncount = ARRAY_SIZE(crossover_fnmap);
