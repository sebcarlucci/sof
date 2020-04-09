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

/*
 * \brief Runs input in through the LR4 filter and returns it's output.
 */
static int32_t crossover_generic_process_lr4(int32_t in,
					     struct iir_state_df2t *lr4)
{
	int32_t z;
	int32_t shift, gain;

	shift = lr4->coef[5];
	gain = lr4->coef[6];
	lr4->coef[5] = 0;
	lr4->coef[6] = 16484;
	/* Cascade two biquads with same coefficients in series */
	z = iir_process_biquad(in, lr4->coef, lr4->delay);
	lr4->coef[5] = shift;
	lr4->coef[6] = gain;
	z = iir_process_biquad(z, lr4->coef, &lr4->delay[2]);
	return z;
}

/*
 * \brief Splits x into two based on the coefficients set in the lp
 * and hp filters. The output of the lp is in y1, the output of
 * the hp is in y2.
 *
 * As a side effect, this function mutates the delay values of both
 * filters.
 */
static void crossover_generic_lr4_split(struct iir_state_df2t *lp,
					struct iir_state_df2t *hp,
					int32_t x, int32_t *y1, int32_t *y2)
{
	*y1 = crossover_generic_process_lr4(x, lp);
	*y2 = crossover_generic_process_lr4(x, hp);
}

static void crossover_generic_split_2way(int32_t in,
					 int32_t out[],
					 struct crossover_state *state)
{
	crossover_generic_lr4_split(&state->lowpass[0], &state->highpass[0],
				    in, &out[0], &out[1]);
}

static void crossover_generic_split_3way(int32_t in,
					 int32_t out[],
					 struct crossover_state *state)
{
	int32_t z;
	
	crossover_generic_lr4_split(&state->lowpass[0], &state->highpass[0],
				    in, &z, &out[2]);
	crossover_generic_lr4_split(&state->lowpass[1], &state->highpass[1],
				    z, &out[0], &out[1]);
}

static void crossover_generic_split_4way(int32_t in,
					 int32_t out[],
					 struct crossover_state *state)
{
	int32_t z1, z2;
	
	crossover_generic_lr4_split(&state->lowpass[1], &state->highpass[1],
				    in, &z1, &z2);
	crossover_generic_lr4_split(&state->lowpass[0], &state->highpass[0],
				    z1, &out[0], &out[1]);
	crossover_generic_lr4_split(&state->lowpass[2], &state->highpass[2],
				    z2, &out[2], &out[3]);
}

#if CONFIG_FORMAT_S16LE
static void crossover_s16_default_pass(const struct comp_dev *dev,
				       const struct comp_buffer *source,
				       struct comp_buffer *sinks[],
				       int32_t num_sinks,
				       uint32_t frames)
{
	const struct audio_stream *source_stream = &source->stream;
	int16_t *x;
	int32_t *y;
	int i, j;
	int n = source_stream->channels * frames;

	for (i = 0; i < n; i++) {
		x = audio_stream_read_frag_s16(source_stream, i);
		for (j = 0; j < num_sinks; j++) {
			y = audio_stream_read_frag_s16((&sinks[j]->stream), i);
			*y = *x;
		}
	}
}
#endif // CONFIG_FORMAT_S16LE

#if CONFIG_FORMAT_S24LE || CONFIG_FORMAT_S32LE
static void crossover_s32_default_pass(const struct comp_dev *dev,
				       const struct comp_buffer *source,
				       struct comp_buffer *sinks[],
				       int32_t num_sinks,
				       uint32_t frames)
{
	const struct audio_stream *source_stream = &source->stream;
	int32_t *x, *y;
	int i, j;
	int n = source_stream->channels * frames;

	for (i = 0; i < n; i++) {
		x = audio_stream_read_frag_s32(source_stream, i);
		for (j = 0; j < num_sinks; j++) {
			y = audio_stream_read_frag_s32((&sinks[j]->stream), i);
			*y = *x;
		}
	}
}
#endif // CONFIG_FORMAT_S24LE || CONFIG_FORMAT_S32LE

#if CONFIG_FORMAT_S16LE
static void crossover_s16_default(const struct comp_dev *dev,
				  const struct comp_buffer *source,
				  struct comp_buffer *sinks[],
				  int32_t num_sinks,
				  uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct crossover_state *state = &cd->state[0];
	const struct audio_stream *source_stream = &source->stream;
	int16_t *x, *y;
	int i, j;
	int idx = 0;
	int nch = source_stream->channels;
	int32_t out[nch];

	for (i = 0; i < frames; i++) {
		x = audio_stream_read_frag_s16(source_stream, idx);
		cd->crossover_split(*x << 16, out, state);
		for (j = 0; j < nch; j++) {
			y = audio_stream_read_frag_s16((&sinks[0]->stream), idx + j);
			*y = sat_int16(Q_SHIFT_RND(out[j], 31, 15));
		}

		idx += nch;
	}
}
#endif // CONFIG_FORMAT_S16LE

#if CONFIG_FORMAT_S24LE
static void crossover_s24_default(const struct comp_dev *dev,
				  const struct comp_buffer *source,
				  struct comp_buffer *sinks[],
				  int32_t num_sinks,
				  uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct crossover_state *state = &cd->state[0];
	const struct audio_stream *source_stream = &source->stream;
	int32_t *x, *y;
	int i, j;
	int idx = 0;
	int nch = source_stream->channels;
	int32_t out[nch];

	for (i = 0; i < frames; i++) {
		x = audio_stream_read_frag_s32(source_stream, idx);
		cd->crossover_split(*x << 8, out, state);
		for (j = 0; j < nch; j++) {
			y = audio_stream_read_frag_s32((&sinks[0]->stream), idx + j);
			*y = sat_int24(Q_SHIFT_RND(out[j], 31, 23));
		}

		idx += nch;
	}
}
#endif // CONFIG_FORMAT_S24LE

#if CONFIG_FORMAT_S32LE
static void crossover_s32_default(const struct comp_dev *dev,
				  const struct comp_buffer *source,
				  struct comp_buffer *sinks[],
				  int32_t num_sinks,
				  uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct crossover_state *state = &cd->state[0];
	const struct audio_stream *source_stream = &source->stream;
	int32_t *x, *y;
	int i, j;
	int idx = 0;
	int nch = source_stream->channels;
	int32_t out[nch];

	for (i = 0; i < frames; i++) {
		x = audio_stream_read_frag_s32(source_stream, idx);
		cd->crossover_split(*x, out, state);
		for (j = 0; j < nch; j++) {
			y = audio_stream_read_frag_s32((&sinks[0]->stream), idx + j);
			*y = out[j];
		}

		idx += nch;
	}
}
#endif // CONFIG_FORMAT_S32LE

const struct crossover_proc_fnmap crossover_proc_fnmap[] = {
/* { SOURCE_FORMAT , PROCESSING FUNCTION } */
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, crossover_s16_default },
#endif /* CONFIG_FORMAT_S16LE */

#if CONFIG_FORMAT_S24LE
	{ SOF_IPC_FRAME_S24_4LE, crossover_s24_default },
#endif /* CONFIG_FORMAT_S24LE */

#if CONFIG_FORMAT_S32LE
	{ SOF_IPC_FRAME_S32_LE, crossover_s32_default },
#endif /* CONFIG_FORMAT_S32LE */
};

const struct crossover_proc_fnmap crossover_proc_fnmap_pass[] = {
/* { SOURCE_FORMAT , PROCESSING FUNCTION } */
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, crossover_s16_default_pass },
#endif /* CONFIG_FORMAT_S16LE */

#if CONFIG_FORMAT_S24LE
	{ SOF_IPC_FRAME_S24_4LE, crossover_s32_default_pass },
#endif /* CONFIG_FORMAT_S24LE */

#if CONFIG_FORMAT_S32LE
	{ SOF_IPC_FRAME_S32_LE, crossover_s32_default_pass },
#endif /* CONFIG_FORMAT_S32LE */
};

const size_t crossover_proc_fncount = ARRAY_SIZE(crossover_proc_fnmap);

const struct crossover_split_fnmap crossover_split_fnmap[] = {
// TODO could this be a Kconfig?
	{ CROSSOVER_TYPE_2WAY, crossover_generic_split_2way },
	{ CROSSOVER_TYPE_3WAY, crossover_generic_split_3way },
	{ CROSSOVER_TYPE_4WAY, crossover_generic_split_4way },
};

const size_t crossover_split_fncount = ARRAY_SIZE(crossover_split_fnmap);
