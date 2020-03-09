// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020 Google LLC. All rights reserved.
//
// Author: Sebastiano Carlucci <scarlucci@google.com>

#include <sof/audio/buffer.h>
#include <sof/audio/component.h>
#include <sof/audio/format.h>
#include <sof/audio/pipeline.h>
#include <sof/audio/crossover/crossover.h>
#include <sof/audio/eq_iir/iir.h>
#include <sof/common.h>
#include <sof/debug/panic.h>
#include <sof/drivers/ipc.h>
#include <sof/lib/alloc.h>
#include <sof/lib/memory.h>
#include <sof/list.h>
#include <sof/platform.h>
#include <sof/string.h>
#include <sof/trace/trace.h>
#include <ipc/control.h>
#include <ipc/stream.h>
#include <ipc/topology.h>
#include <user/trace.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

static const struct comp_driver comp_crossover;

/* FOR TESTING ONLY - REMOVE THIS !!!!!!!! */
int lp0[] = {0xc253826f,0x7da1773e,0x000161a1,0x0002c342,0x000161a1};
int lp1[] = {0xcad0cdef,0x742e8c5d,0x00202837,0x0040506d,0x00202837};
int lp2[] = {0xe16f20ea,0x51e57f66,0x01966267,0x032cc4ce,0x01966267};

int hp0[] = {0xc253826f,0x7da1773e,0x1f7cd6e3,0xc106523b,0x1f7cd6e3};
int hp1[] = {0xcad0cdef,0x742e8c5d,0x1d3d7328,0xc58519af,0x1d3d7328};
int hp2[] = {0xe16f20ea,0x51e57f66,0x161c344b,0xd3c7976a,0x161c344b};

int *lp[] = {lp0, lp1, lp2};
int *hp[] = {hp0, hp1, hp2};
/* DO NOT FORGET TO REMOVE */

static void crossover_reset_lr4_state(lr4_state *lr4)
{
        lr4->coef = NULL;
        rfree(lr4->delay);
        lr4->delay = NULL;
}

/**
 * Sets the state of a single LR4 filter.
 * An LR4 filter is built by cascading two biquads in series.
 */
static inline int crossover_setup_lr4(int32_t *coef, lr4_state *lr4)
{
        lr4->biquads = 2;
        lr4->biquads_in_series = 2;
        lr4->coef = coef;
        /* LR4 filters are 4th order filters, so only need 4 delay slots */
        lr4->delay = rzalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM,
                             sizeof(uint32_t) * 4);
        if (!lr4->delay)
                return -ENOMEM;
        return 0;
}

/**
 * Initializes the coefficients and delay of the Crossover audio component.
 */
static int crossover_setup(struct comp_data *cd)
{
        struct crossover_state *state;
        int ret;
        int i;
        int j;

        /* TODO USE CONFIG FOR THISSSSSSSSSS */
        cd->num_sinks = 2;
        /*
         * Each Crossover channel is made up of three low pass and three
         * high pass LR4 filters.
         */
        for (i = 0; i < PLATFORM_MAX_CHANNELS; i++) {
                state = &cd->state[i];

                /* TODO USE CONFIG FOR THISSSSSSSSSS */
                for (j = 0; j < 3; j++) {
                        ret = crossover_setup_lr4(lp[j], &state->lowpass[j]);
                        if (ret < 0)
                                return ret;

                        ret = crossover_setup_lr4(hp[j], &state->highpass[j]);
                        if (ret < 0)
                                return ret;
                }
        }
}

static void crossover_free_config(struct sof_crossover_config **config)
{
        rfree(*config);
        *config = NULL;
}

/**
 * \brief Creates a Crossover Filter component.
 * \return Pointer to Crossover Filter component device.
 */
static struct comp_dev *crossover_new(const struct comp_driver *drv,
                                      struct sof_ipc_comp *comp)
{
        struct comp_dev *dev;
        struct comp_data *cd;
        struct sof_ipc_comp_process *crossover;
        struct sof_ipc_comp_process *ipc_crossover =
                (struct sof_ipc_comp_process *) comp;
        size_t bs = ipc_crossover->size;
        int ret;

        comp_cl_info(&comp_crossover, "crossover_new()");

        /* Check that the coefficients blob size is sane. It should have the
         * coefficients for 6 biquads
         */
        if (bs > 0) {
                comp_cl_err(&comp_crossover, "crossover_new(), NOT IMPLEMENTED YET");
                return NULL;
        }

        dev = rzalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM,
                      COMP_SIZE(struct sof_ipc_comp_process));
        if (!dev)
                return NULL;
        dev->drv = drv;

        dev->size = COMP_SIZE(struct sof_ipc_comp_process);

        crossover = COMP_GET_IPC(dev, sof_ipc_comp_process);
        ret = memcpy_s(crossover, sizeof(*crossover), ipc_crossover,
                       sizeof(struct sof_ipc_comp_process));
        assert(!ret);

        cd = rzalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM, sizeof(*cd));
        if (!cd) {
                rfree(dev);
                return NULL;
        }

        comp_set_drvdata(dev, cd);

        cd->crossover_func = NULL;
        cd->config = NULL;
        cd->config_new = NULL;

        if (bs) {
                cd->config = rzalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM,
                                     bs);
                if (!cd->config) {
                        rfree(dev);
                        rfree(cd);
                        return NULL;
                }

                ret = memcpy_s(cd->config, bs, ipc_crossover->data, bs);
                assert(!ret);
        } else {
                /* FOR TESTING RIGHT NOW. REMOVE ONCE FINISHED IMPLEMENTING CONFIG */
                // If null then use test structs
        }

        dev->state = COMP_STATE_READY;
        return dev;
}

/**
 * \brief Frees Crossover Filter component.
 * \param[in,out] dev Crossover filter base component device.
 */
static void crossover_free(struct comp_dev *dev)
{
        struct comp_data *cd = comp_get_drvdata(dev);

        comp_info(dev, "crossover_free()");

        if (cd->config)
                crossover_free_config(&cd->config);
        if (cd->config_new)
                crossover_free_config(&cd->config_new);

        rfree(cd);
        rfree(dev);
}

/**
 * \brief Sets Crossover Filter component audio stream parameters.
 * \param[in,out] dev Crossover Filter base component device.
 * \return Error code.
 *
 * All done in prepare() since we need to know source and sink component params.
 */
static int crossover_params(struct comp_dev *dev,
                            struct sof_ipc_stream_params *params)
{
	comp_info(dev, "crossover_params()");

	return 0;
}

/**
 * \brief Handles incoming IPC commands for Crossover component.
 */
static int crossover_cmd(struct comp_dev *dev, int cmd, void *data,
                         int max_data_size)
{
        comp_info(dev, "crossover_cmd()");

        return 0;
}

/**
 * \brief Sets Crossover Filter component state.
 * \param[in,out] dev Crossover Filter base component device.
 * \param[in] cmd Command type.
 * \return Error code.
 */
static int crossover_trigger(struct comp_dev *dev, int cmd)
{
	comp_info(dev, "crossover_trigger()");

	return comp_set_state(dev, cmd);
}

/**
 * \brief Copies and processes stream data.
 * \param[in,out] dev Crossover Filter base component device.
 * \return Error code.
 */
static int crossover_copy(struct comp_dev *dev)
{
        struct comp_copy_limits cl;
        struct comp_data *cd = comp_get_drvdata(dev);
        struct comp_buffer *source;
        struct comp_buffer *sink;
        struct comp_buffer *sinks[CROSSOVER_MAX_STREAMS] = { NULL };
        struct list_item *clist;
        int ret;
        uint32_t flags = 0;

        comp_dbg(dev, "crossover_copy()");

        source = list_first_item(&dev->bsource_list, struct comp_buffer,
                                  sink_list);

        // align sink streams with their respective configurations
  	list_for_item(clist, &dev->bsink_list) {
  		sink = container_of(clist, struct comp_buffer, source_list);
  		if (sink->sink->state == dev->state) {
  			num_sinks++;
  			i = get_stream_index(cd, sink->pipeline_id);
  			sinks[i] = sink;
  		}
  	}

        /* Check for changed configuration */
        if (cd->config_new) {
                crossover_free_config(&cd->config);
                cd->config = cd->config_new;
                cd->config_new = NULL;
        }

        return 0;
}

/**
 * \brief Prepares Crossover Filter component for processing.
 * \param[in,out] dev Crossover Filter base component device.
 * \return Error code.
 */
static int crossover_prepare(struct comp_dev *dev)
{
        struct comp_data *cd = comp_get_drvdata(dev);
        struct comp_buffer *source;
        struct comp_buffer *sink;

        int32_t num_sinks = 0;
        int ret;

        comp_info(dev, "crossover_prepare()");

        ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
        if (ret < 0)
                return ret;

        if(ret == COMP_STATUS_STATE_ALREADY_SET)
                return PPL_STATUS_PATH_STOP;

        /* Crossover has a variable number of sinks. Assume that sink buffers
         * have same frame_fmt and size.
         */
        source = list_first_item(&dev->bsource_list,
				  struct comp_buffer, sink_list);
	sink = list_first_item(&dev->bsink_list,
				struct comp_buffer, source_list);

	/* get source data format */
	cd->source_format = source->stream.frame_fmt;

	/* get sink data format and period bytes */
	cd->sink_format = sink->stream.frame_fmt;
	sink_period_bytes = audio_stream_period_bytes(&sink->stream,
						      dev->frames);

	if (sink->stream.size < config->periods_sink * sink_period_bytes) {
		comp_err(dev, "crossover_prepare(), sink buffer size %d is insufficient",
			 sink->stream.size);
		ret = -ENOMEM;
		goto err;
	}

        comp_info(dev, "crossover_prepare(), source_format=%d, sink_format=%d",
		  cd->source_format, cd->sink_format);

        /* Initialize Crossover */
        if (cd->config) {
                ret = crossover_setup(cd);
                if (ret < 0) {
                        comp_err(dev, "crossover_prepare(), setup failed");
                        return ret;
                }

                cd->crossover_func = crossover_find_fund(cd->source_format);
                if (!cd->crossover_func) {
                        comp_err(dev, "crossover_prepare(), No processing function matching frame_fmt %i",
                                 cd->source_format);
                        ret = -EINVAL;
                        goto err;
                }

        } else {
                /* TODO NOW FOR TESTING ONLY */
                ret = crossover_setup(cd);
                if (ret < 0) {
                        comp_err(dev, "crossover_prepare(), setup failed");
                        return ret;
                }
                cd->crossover_func = crossover_find_fund(cd->source_format);
                if (!cd->crossover_func) {
                        comp_err(dev, "crossover_prepare(), No processing function matching frame_fmt %i",
                                 cd->source_format);
                        ret = -EINVAL;
                        goto err;
                }
        }

        return 0;

err:
        comp_set_state(dev, COMP_TRIGGER_RESET);
        return ret;
}

/**
 * \brief Resets Crossover Filter component.
 * \param[in,out] dev Crossover Filter base component device.
 * \return Error code.
 */
static int crossover_reset(struct comp_dev *dev)
{
        comp_info(dev, "crossover_reset()");

        return 0;
}

 /** \brief Crossover Filter component definition. */
static const struct comp_driver comp_crossover = {
	.type	= SOF_COMP_CROSSOVER,
	.ops	= {
		.new		= crossover_new,
		.free		= crossover_free,
		.params		= crossover_params,
		.cmd		= crossover_cmd,
		.trigger	= crossover_trigger,
		.copy		= crossover_copy,
		.prepare	= crossover_prepare,
		.reset		= crossover_reset,
	},
};

static SHARED_DATA struct comp_driver_info comp_crossover_info = {
	.drv = &comp_crossover,
};

static void sys_comp_crossover_init(void)
{
	comp_register(platform_shared_get(&comp_crossover_info,
                                          sizeof(comp_crossover_info)));
}

DECLARE_MODULE(sys_comp_crossover_init);
