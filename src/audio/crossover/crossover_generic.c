// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020 Google LLC. All rights reserved.
//
// Author: Sebastiano Carlucci <scarlucci@google.com>

#include <stdint.h>
#include <sof/audio/component.h>
#include <sof/audio/format.h>
#include <sof/audio/crossover/crossover.h>

static void crossover_generic(struct crossover_state *state,
                              int32_t x, int32_t y[], int32_t num_sinks)
{
        /* PASS THROUGH FOR NOW */
        for (int i = 0; i < num_sinks; i++) {
                y[i] = x;
        }
}

static void crossover_s16_default(const struct comp_dev *dev,
                                  const struct audio_stream *source,
				  struct audio_stream *sinks[],
                                  int32_t num_sinks,
				  uint32_t frames)
{
        struct comp_data *cd = comp_get_drvdata(dev);
	struct crossover_state *state;
	int16_t *x;
	int16_t *y;
	int32_t tmp_out[num_sinks];
	int idx;
	int ch;
	int i;
        int j;
	int nch = source->channels;

	for (ch = 0; ch < nch; ch++) {
		state = &cd->state[ch];
		idx = ch;
		for (i = 0; i < frames; i++) {
			x = audio_stream_read_frag_s16(source, idx);
                        /* Store output in temporary array */
			crossover_generic(state, *x << 16, tmp_out, num_sinks);

                        for (j = 0; j < num_sinks; j++) {
                                y = audio_stream_read_frag_s16(sinks[j], idx);
        			*y = sat_int16(Q_SHIFT_RND(tmp_out[j], 31, 15));
                        }
			idx += nch;
		}
	}
}

const struct crossover_func_map crossover_fnmap[] = {
/* { SOURCE_FORMAT , PROCESSING FUNCTION } */
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, crossover_s16_default },
#endif /* CONFIG_FORMAT_S16LE */
// #if CONFIG_FORMAT_S24LE
// 	{ SOF_IPC_FRAME_S24_4LE, crossover_s24_default },
// #endif /* CONFIG_FORMAT_S24LE */
// #if CONFIG_FORMAT_S32LE
// 	{ SOF_IPC_FRAME_S32_LE, crossover_s32_default },
// #endif /* CONFIG_FORMAT_S32LE */
};

const size_t crossover_fncount = ARRAY_SIZE(crossover_fnmap);
