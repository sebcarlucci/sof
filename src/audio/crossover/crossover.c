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
#include <sof/lib/uuid.h>
#include <sof/list.h>
#include <sof/platform.h>
#include <sof/string.h>
#include <sof/trace/trace.h>
#include <ipc/control.h>
#include <ipc/stream.h>
#include <ipc/topology.h>
#include <user/trace.h>
#include <user/crossover.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

static const struct comp_driver comp_crossover;

/* 948c9ad1-806a-4131-ad6c-b2bda9e35a9f */
DECLARE_SOF_UUID("crossover", crossover_uuid, 0x948c9ad1, 0x806a, 0x4131,
		 0xad, 0x6c, 0xb2, 0xbd, 0xa9, 0xe3, 0x5a, 0x9f);

/* FOR TESTING ONLY - REMOVE THIS !!!!!!!! */
int lp0[] = {0xc253826f, 0x7da1773e, 0x000161a1, 0x0002c342, 0x000161a1, 0, 16484};
int lp1[] = {0xcad0cdef, 0x742e8c5d, 0x00202837, 0x0040506d, 0x00202837, 0, 16484};
int lp2[] = {0xe16f20ea, 0x51e57f66, 0x01966267, 0x032cc4ce, 0x01966267, 0, 16484};

int hp0[] = {0xc253826f, 0x7da1773e, 0x1f7cd6e3, 0xc106523b, 0x1f7cd6e3, 0, 16484};
int hp1[] = {0xcad0cdef, 0x742e8c5d, 0x1d3d7328, 0xc58519af, 0x1d3d7328, 0, 16484};
int hp2[] = {0xe16f20ea, 0x51e57f66, 0x161c344b, 0xd3c7976a, 0x161c344b, 0, 16484};

int *lp[] = {lp0, lp1, lp2};
int *hp[] = {hp0, hp1, hp2};
/* DO NOT FORGET TO REMOVE */

static void crossover_free_config(struct sof_crossover_config **config)
{
		rfree(*config);
		*config = NULL;
}

static void crossover_reset_state_lr4(lr4_state *lr4)
{
	rfree(lr4->delay);

	lr4->coef = NULL;
	lr4->delay = NULL;
}

static void crossover_reset_state_ch(struct crossover_state *ch_state)
{
	int i;

	for (i = 0; i < 3; i++) {
		crossover_reset_state_lr4(&ch_state->lowpass[i]);
		crossover_reset_state_lr4(&ch_state->highpass[i]);
	}
}

static void crossover_reset_state(struct comp_data *cd)
{
	int i;

	for (i = 0; i < PLATFORM_MAX_CHANNELS; i++)
		crossover_reset_state_ch(&cd->state[i]);
}

/**
 * The assign_sink array in the configuration maps to the pipeline ids.
 * This function  returns the index i such that assign_sink[i] = pipe_id
 */
static uint8_t get_stream_index(struct sof_crossover_config *config,
				uint32_t pipe_id)
{
	int i;
	int32_t *assign_sink = &config->data[0];

	for (i = 0; i < config->num_sinks; i++)
		if (assign_sink[i] == pipe_id)
			return i;

	comp_cl_err(&comp_crossover, "get_stream_index() error: couldn't find configuration for connected pipeline %u",
		    pipe_id);

	return -EINVAL;
}

static int crossover_assign_sinks(struct comp_dev *dev,
				  struct sof_crossover_config *config,
				  struct comp_buffer **sinks)
{
	struct comp_buffer *sink;
	struct list_item *clist;
	int num_sinks = 0;
	int i;

	// align sink streams with their respective configurations
	list_for_item(clist, &dev->bsink_list) {
		sink = container_of(clist, struct comp_buffer, source_list);
		if (sink->sink->state == dev->state) {
			// if no config is set, then assign the sinks in order
			if (!config) {
				sinks[num_sinks++] = sink;
				continue;
			}

			i = get_stream_index(config, sink->pipeline_id);
			if (i < 0) {
				comp_warn(dev, "crossover_assign_sinks(), could not assign sink %i",
					  sink->pipeline_id);
				continue;
			}

			if (sinks[i]) {
				comp_warn(dev, "crossover_assign_sinks(), multiple sinks from pipeline %i are assigned",
					  sink->pipeline_id);
				continue;
			}

			sinks[i] = sink;
			num_sinks++;
		}
	}

	return num_sinks;
}

/**
 * Sets the state of a single LR4 filter.
 * An LR4 filter is built by cascading two biquads in series.
 */
static int crossover_init_coef_lr4(struct sof_crossover_config_lr4 *coef,
				   lr4_state *lr4)
{
	/* Reuse the same coefficients for biquads so we only
	 * store one copy of it. The processing functions will
	 * feed the same coef array to both biquads.
	 */
	lr4->coef = (int32_t *)coef;

	/* LR4 filters are two 2nd order filters, so only need 4 delay slots
	 * delay[0..1] -> state for first biquad
	 * delay[2..3] -> state for second biquad
	 */
	lr4->delay = rzalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM,
			     sizeof(uint64_t) * 4);
	if (!lr4->delay)
		return -ENOMEM;
	return 0;
}

static void crossover_init_coef_ch(struct sof_crossover_config_lr4 *coef,
				   struct crossover_state *state,
				   int32_t num_sinks)
{
	int32_t i;
	int32_t j = 0;

	for (i = 0; i < num_sinks; i++) {
		/* Get the low pass coefficients */
		crossover_init_coef_lr4(&coef[j],
					&state->lowpass[i]);
		/* Get the high pass coefficients */
		crossover_init_coef_lr4(&coef[j + 1],
					&state->highpass[i]);
		j += 2;
	}
}

static int crossover_init_coef(struct comp_data *cd, int nch)
{
	struct sof_crossover_config_lr4 *lookup[SOF_CROSSOVER_MAX_RESPONSES] = { NULL };
	struct sof_crossover_config_lr4 *crossover;
	struct sof_crossover_config *config = cd->config;
	int32_t *assign_response;
	int32_t *coef_data;
	int32_t channels_in_config = config->channels_in_config;
	int32_t number_of_responses = config->number_of_responses;
	int32_t num_sinks = config->num_sinks;
	int resp = 0;
	int i;
	int j;

	if (!config) {
		comp_cl_err(&comp_crossover, "crossover_init_coef(), no config is set");
		return -EINVAL;
	}

	channels_in_config = config->channels_in_config;
	number_of_responses = config->number_of_responses;
	num_sinks = config->num_sinks;

	comp_cl_info(&comp_crossover, "crossover_init_coef(), response assign for %u channels, %u responses",
		     channels_in_config,
		     number_of_responses);

	/* Sanity checks */
	if (nch > PLATFORM_MAX_CHANNELS ||
	    channels_in_config > PLATFORM_MAX_CHANNELS ||
	    !channels_in_config) {
		comp_cl_err(&comp_crossover, "crossover_init_coef(), invalid channels count (%i)",
			    nch);
		return -EINVAL;
	}

	if (number_of_responses > SOF_CROSSOVER_MAX_RESPONSES) {
		comp_cl_err(&comp_crossover, "crossover_init_coef(), # of resp (%i) exceeds max (%i)",
			    number_of_responses, SOF_CROSSOVER_MAX_RESPONSES);
		return -EINVAL;
	}

	/* Collect the assign_response and coeff array */
	assign_response = ASSUME_ALIGNED(&config->data[num_sinks], 4);
	coef_data = ASSUME_ALIGNED(&config->data[channels_in_config + num_sinks], 4);

	/* Assign the i-th responses in the blob to the i-th entry
	 * in the lookup table
	 */
	j = 0;
	for (i = 0; i < SOF_CROSSOVER_MAX_RESPONSES; i++) {
		if (i < number_of_responses) {
			crossover = (struct sof_crossover_config_lr4 *)&coef_data[j];
			lookup[i] = crossover;
			j += SOF_CROSSOVER_COEF_SIZE(num_sinks);
		}
	}

	/* Initialize 1st phase */
	for (i = 0; i < nch; i++) {
		/* Check for not reading past blob response to channel assign
		 * map. The previous channel response is assigned for any
		 * additional channels in the stream. It allows to use single
		 * channel configuration to setup the crossover for multiple
		 * channels with the same response.
		 */
		if (i < channels_in_config)
			resp = assign_response[i];

		if (resp < 0) {
			/* Initialize the i-th channel as pass through to all
			 * the sinks.
			 */
			comp_cl_warn(&comp_crossover, "crossover_init_coef(), ch %d is set to bypass", i);
			crossover_reset_state_ch(&cd->state[i]);
			continue;
		}

		if (resp >= config->number_of_responses) {
			comp_cl_warn(&comp_crossover, "crossover_init_coef(), requested response %d exceeds defined %d",
				     resp, number_of_responses);
			crossover_reset_state_ch(&cd->state[i]);
			continue;
		}

		/* Assign crossover coefficients for channel i */
		crossover = lookup[resp];
		crossover_init_coef_ch(crossover, &cd->state[i], num_sinks);
		comp_cl_info(&comp_crossover, "crossover_init_coef(), ch %d is set to response %d",
			     i, resp);
	}

	return 0;
}

/**
 * Initializes the coefficients and delay of the Crossover audio component.
 */
static int crossover_setup(struct comp_data *cd, int nch)
{
	int ret = 0;

	/* Reset any previous state. */
	crossover_reset_state(cd);

	/* Assign LR4 coefficients from config */
	ret = crossover_init_coef(cd, nch);

	return ret;
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
			(struct sof_ipc_comp_process *)comp;
	size_t bs = ipc_crossover->size;
	int ret;

	comp_cl_info(&comp_crossover, "crossover_new()");

	/* Check that the coefficients blob size is sane.
	 * It should have the coefficients for 6 biquads.
	 * TODO Have the check somehow
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

	cd = rzalloc(SOF_MEM_ZONE_RUNTIME, 0,
		     SOF_MEM_CAPS_RAM, sizeof(*cd));
	if (!cd) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, cd);

	cd->crossover_func = NULL;
	cd->config = NULL;
	cd->config_new = NULL;

	if (bs) {
		cd->config = rzalloc(SOF_MEM_ZONE_RUNTIME, 0,
				     SOF_MEM_CAPS_RAM, bs);
		if (!cd->config) {
			rfree(dev);
			rfree(cd);
			return NULL;
		}

		ret = memcpy_s(cd->config, bs, ipc_crossover->data, bs);
		assert(!ret);
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

	crossover_free_config(&cd->config);
	crossover_free_config(&cd->config_new);

	crossover_reset_state(cd);

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

static int crossover_cmd_get_data(struct comp_dev *dev,
				  struct sof_ipc_ctrl_data *cdata, int max_size)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	size_t bs;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		comp_info(dev, "crossover_cmd_get_data(), SOF_CTRL_CMD_BINARY");

		/* Copy back to user space */
		if (cd->config) {
			bs = cd->config->size;
			comp_info(dev, "crossver_cmd_get_data(), size %u",
				  bs);
			if (bs > SOF_CROSSOVER_MAX_SIZE || bs == 0 ||
			    bs > max_size)
				return -EINVAL;
			ret = memcpy_s(cdata->data->data,
				       ((struct sof_abi_hdr *)(cdata->data))->size,
				       cd->config, bs);
			assert(!ret);

			cdata->data->abi = SOF_ABI_VERSION;
			cdata->data->size = bs;
		} else {
			comp_err(dev, "crossover_cmd_get_data(), no config");
			ret = -EINVAL;
		}
		break;
	default:
		comp_err(dev, "crossover_cmd_get_data(), invalid command");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int crossover_cmd_set_data(struct comp_dev *dev,
				  struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_crossover_config *request;
	size_t bs;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		comp_info(dev, "crososver_cmd_set_data(), SOF_CTRL_CMD_BINARY");

		/* Find size from header */
		request = (struct sof_crossover_config *)cdata->data->data;
		bs = request->size;
		if (bs > SOF_CROSSOVER_MAX_SIZE || bs == 0) {
			comp_err(dev, "crossover_cmd_set_data(), size %d is invalid",
				 bs);
			return -EINVAL;
		}

		/* Check that there is no work-in-progress previous request */
		if (cd->config_new) {
			comp_err(dev, "crossover_cmd_set_data(), busy with previous");
			return -EBUSY;
		}

		/* Allocate and make a copy of the blob */
		cd->config_new = rzalloc(SOF_MEM_ZONE_RUNTIME, 0,
					 SOF_MEM_CAPS_RAM, bs);
		if (!cd->config_new) {
			comp_err(dev, "crossover_cmd_set_data(), alloc fail");
			return -EINVAL;
		}

		/* Copy the configuration. If the component state is ready
		 * the Crossover will initialize in prepare().
		 */
		ret = memcpy_s(cd->config_new, bs, cdata->data->data, bs);
		assert(!ret);

		/* If component state is READY we can omit old configuration
		 * immediately. When in playback/capture the new configuration
		 * presence is checked in copy().
		 */
		if (dev->state == COMP_STATE_READY)
			crossover_free_config(&cd->config);

		/* If there is no existing configuration the received can
		 * be set to current immediately. It will be applied in
		 * prepare() when streaming starts.
		 */
		if (!cd->config) {
			cd->config = cd->config_new;
			cd->config_new = NULL;
		}

		break;
	default:
		comp_err(dev, "crossover_cmd_set_data(), invalid command");
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * \brief Handles incoming IPC commands for Crossover component.
 */
static int crossover_cmd(struct comp_dev *dev, int cmd, void *data,
			 int max_data_size)
{
	struct sof_ipc_ctrl_data *cdata = data;
	int ret = 0;

		comp_info(dev, "crossover_cmd()");

	switch (cmd) {
	case COMP_CMD_SET_DATA:
		ret = crossover_cmd_set_data(dev, cdata);
		break;
	case COMP_CMD_GET_DATA:
		ret = crossover_cmd_get_data(dev, cdata, max_data_size);
		break;
	default:
		comp_err(dev, "crossover_cmd(), invalid command");
		ret = -EINVAL;
	}

	return ret;
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
	struct comp_data *cd = comp_get_drvdata(dev);
	struct comp_buffer *source;
	struct comp_buffer *sinks[CROSSOVER_MAX_STREAMS] = { NULL };
	int i;
	uint32_t num_sinks = 0;
	uint32_t frames = -1;
	uint32_t source_bytes;
	uint32_t avail;
	uint32_t flags = 0;
	uint32_t sinks_bytes[CROSSOVER_MAX_STREAMS] = { 0 };

	comp_dbg(dev, "crossover_copy()");

	source = list_first_item(&dev->bsource_list, struct comp_buffer,
				 sink_list);

	/* Check for changed configuration */
	if (cd->config_new) {
		crossover_free_config(&cd->config);
		cd->config = cd->config_new;
		cd->config_new = NULL;
		/* TODO update coefficients with new config */
	}

	/* Use the assign_sink array from the config to route
	 * the output to the corresponding sinks
	 */
	num_sinks = crossover_assign_sinks(dev, cd->config, sinks);

	buffer_lock(source, &flags);

	/* check if source is active */
	if (source->source->state != dev->state) {
		buffer_unlock(source, flags);
		return -EINVAL;
	}

	for (i = 0; i < CROSSOVER_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		buffer_lock(sinks[i], &flags);
		avail = audio_stream_avail_frames(&source->stream,
						  &sinks[i]->stream);
		frames = MIN(frames, avail);
		buffer_unlock(sinks[i], flags);
	}

	buffer_unlock(source, flags);

	source_bytes = frames * audio_stream_frame_bytes(&source->stream);

	for (i = 0; i < CROSSOVER_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		sinks_bytes[i] = frames *
				 audio_stream_frame_bytes(&sinks[i]->stream);
	}

	cd->crossover_func(dev, source, sinks, num_sinks, frames);

	/* update components */
	for (i = 0; i < CROSSOVER_MAX_STREAMS; i++) {
		if (!sinks[i])
			continue;
		comp_update_buffer_produce(sinks[i], sinks_bytes[i]);
	}
	comp_update_buffer_consume(source, source_bytes);

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
	struct sof_ipc_comp_config *config = dev_comp_config(dev);
	struct comp_buffer *source;
	struct comp_buffer *sink;
	int32_t sink_period_bytes;
	int ret;

	comp_info(dev, "crossover_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
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

	comp_info(dev, "crossover_prepare(), source_format=%d, sink_format=%d, nch=%d",
		  cd->source_format, cd->sink_format, source->stream.channels);

	/* Initialize Crossover */
	if (cd->config) {
		ret = crossover_setup(cd, source->stream.channels);
		if (ret < 0) {
			comp_err(dev, "crossover_prepare(), setup failed");
			return ret;
		}

		cd->crossover_func = crossover_find_func(cd->source_format);
		if (!cd->crossover_func) {
			comp_err(dev, "crossover_prepare(), No processing function matching frame_fmt %i",
				 cd->source_format);
			ret = -EINVAL;
			goto err;
		}

	} else {
		comp_info(dev, "crossover_prepare(), setting crossover to passthrough mode");

		cd->crossover_func =
			crossover_find_func_pass(cd->source_format);

		if (!cd->crossover_func) {
			comp_err(dev, "crossover_prepare(), No passthrough function matching frame_fmt %i",
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
	.uid	= SOF_UUID(crossover_uuid),
	.ops	= {
		.create		= crossover_new,
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
