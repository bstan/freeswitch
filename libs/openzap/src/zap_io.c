/*
 * Copyright (c) 2007, Anthony Minessale II
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
#ifndef WIN32
#endif
#include "openzap.h"
//#include "zap_isdn.h"
//#include "zap_ss7_boost.h"
#include <stdarg.h>
#ifdef WIN32
#include <io.h>
#endif
#ifdef ZAP_PIKA_SUPPORT
#include "zap_pika.h"
#endif

static int time_is_init = 0;

static void time_init(void)
{
#ifdef WIN32
	timeBeginPeriod(1);
#endif
	time_is_init = 1;
}

static void time_end(void)
{
#ifdef WIN32
	timeEndPeriod(1);
#endif
	time_is_init = 0;
}

zap_time_t zap_current_time_in_ms(void)
{
#ifdef WIN32
	return timeGetTime();
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
#endif
}

static struct {
	zap_hash_t *interface_hash;
	zap_hash_t *module_hash;
	zap_hash_t *span_hash;
	zap_mutex_t *mutex;
	struct zap_span *spans[ZAP_MAX_SPANS_INTERFACE+1];
	uint32_t span_index;
	uint32_t running;
} globals;


/* enum lookup funcs */
ZAP_ENUM_NAMES(TONEMAP_NAMES, TONEMAP_STRINGS)
ZAP_STR2ENUM(zap_str2zap_tonemap, zap_tonemap2str, zap_tonemap_t, TONEMAP_NAMES, ZAP_TONEMAP_INVALID)

ZAP_ENUM_NAMES(OOB_NAMES, OOB_STRINGS)
ZAP_STR2ENUM(zap_str2zap_oob_event, zap_oob_event2str, zap_oob_event_t, OOB_NAMES, ZAP_OOB_INVALID)

ZAP_ENUM_NAMES(TRUNK_TYPE_NAMES, TRUNK_STRINGS)
ZAP_STR2ENUM(zap_str2zap_trunk_type, zap_trunk_type2str, zap_trunk_type_t, TRUNK_TYPE_NAMES, ZAP_TRUNK_NONE)

ZAP_ENUM_NAMES(START_TYPE_NAMES, START_TYPE_STRINGS)
ZAP_STR2ENUM(zap_str2zap_analog_start_type, zap_analog_start_type2str, zap_analog_start_type_t, START_TYPE_NAMES, ZAP_ANALOG_START_NA)

ZAP_ENUM_NAMES(SIGNAL_NAMES, SIGNAL_STRINGS)
ZAP_STR2ENUM(zap_str2zap_signal_event, zap_signal_event2str, zap_signal_event_t, SIGNAL_NAMES, ZAP_SIGEVENT_INVALID)

ZAP_ENUM_NAMES(CHANNEL_STATE_NAMES, CHANNEL_STATE_STRINGS)
ZAP_STR2ENUM(zap_str2zap_channel_state, zap_channel_state2str, zap_channel_state_t, CHANNEL_STATE_NAMES, ZAP_CHANNEL_STATE_INVALID)

ZAP_ENUM_NAMES(MDMF_TYPE_NAMES, MDMF_STRINGS)
ZAP_STR2ENUM(zap_str2zap_mdmf_type, zap_mdmf_type2str, zap_mdmf_type_t, MDMF_TYPE_NAMES, MDMF_INVALID)

ZAP_ENUM_NAMES(CHAN_TYPE_NAMES, CHAN_TYPE_STRINGS)
ZAP_STR2ENUM(zap_str2zap_chan_type, zap_chan_type2str, zap_chan_type_t, CHAN_TYPE_NAMES, ZAP_CHAN_TYPE_COUNT)


static const char *cut_path(const char *in)
{
	const char *p, *ret = in;
	char delims[] = "/\\";
	char *i;

	for (i = delims; *i; i++) {
		p = in;
		while ((p = strchr(p, *i)) != 0) {
			ret = ++p;
		}
	}
	return ret;
}

static void null_logger(const char *file, const char *func, int line, int level, const char *fmt, ...)
{
	if (file && func && line && level && fmt) {
		return;
	}
	return;
}


static const char *LEVEL_NAMES[] = {
	"EMERG",
	"ALERT",
	"CRIT",
	"ERROR",
	"WARNING",
	"NOTICE",
	"INFO",
	"DEBUG",
	NULL
};

static int zap_log_level = 7;

static void default_logger(const char *file, const char *func, int line, int level, const char *fmt, ...)
{
	const char *fp;
	char data[1024];
	va_list ap;

	if (level < 0 || level > 7) {
		level = 7;
	}
	if (level > zap_log_level) {
		return;
	}
	
	fp = cut_path(file);

	va_start(ap, fmt);

	vsnprintf(data, sizeof(data), fmt, ap);


	fprintf(stderr, "[%s] %s:%d %s() %s", LEVEL_NAMES[level], file, line, func, data);

	va_end(ap);

}

zap_logger_t zap_log = null_logger;

void zap_global_set_logger(zap_logger_t logger)
{
	if (logger) {
		zap_log = logger;
	} else {
		zap_log = null_logger;
	}
}

void zap_global_set_default_logger(int level)
{
	if (level < 0 || level > 7) {
		level = 7;
	}

	zap_log = default_logger;
	zap_log_level = level;
}

int zap_hash_equalkeys(void *k1, void *k2)
{
    return strcmp((char *) k1, (char *) k2) ? 0 : 1;
}

uint32_t zap_hash_hashfromstring(void *ky)
{
	unsigned char *str = (unsigned char *) ky;
	uint32_t hash = 0;
    int c;
	
	while ((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
	}

    return hash;
}


static zap_status_t zap_span_destroy(zap_span_t *span)
{
	zap_status_t status = ZAP_FAIL;
	
	if (zap_test_flag(span, ZAP_SPAN_CONFIGURED) && span->zio && span->zio->span_destroy) {
		zap_log(ZAP_LOG_INFO, "Destroying span %u type (%s)\n", span->span_id, span->type);
		status = span->zio->span_destroy(span);
		zap_safe_free(span->type);
		zap_safe_free(span->dtmf_hangup);
	}

	return status;
}

static zap_status_t zap_channel_destroy(zap_channel_t *zchan)
{

	if (zap_test_flag(zchan, ZAP_CHANNEL_CONFIGURED)) {

		while (zap_test_flag(zchan, ZAP_CHANNEL_INTHREAD)) {
			zap_log(ZAP_LOG_INFO, "Waiting for thread to exit on channel %u:%u\n", zchan->span_id, zchan->chan_id);
			zap_sleep(500);
		}

		zap_buffer_destroy(&zchan->digit_buffer);
		zap_buffer_destroy(&zchan->gen_dtmf_buffer);
		zap_buffer_destroy(&zchan->dtmf_buffer);
		zap_buffer_destroy(&zchan->fsk_buffer);
		hashtable_destroy(zchan->variable_hash);

		zap_safe_free(zchan->dtmf_hangup_buf);

		if (zchan->tone_session.buffer) {
			teletone_destroy_session(&zchan->tone_session);
			memset(&zchan->tone_session, 0, sizeof(zchan->tone_session));
		}

		
		if (zchan->span->zio->channel_destroy) {
			zap_log(ZAP_LOG_INFO, "Closing channel %s:%u:%u fd:%d\n", zchan->span->type, zchan->span_id, zchan->chan_id, zchan->sockfd);
			if (zchan->span->zio->channel_destroy(zchan) == ZAP_SUCCESS) {
				zap_clear_flag_locked(zchan, ZAP_CHANNEL_CONFIGURED);
			} else {
				zap_log(ZAP_LOG_ERROR, "Error Closing channel %u:%u fd:%d\n", zchan->span_id, zchan->chan_id, zchan->sockfd);
			}
		}

		zap_mutex_destroy(&zchan->mutex);
	}
	
	return ZAP_SUCCESS;
}



zap_status_t zap_channel_get_alarms(zap_channel_t *zchan)
{
	zap_status_t status = ZAP_FAIL;

	if (zap_test_flag(zchan, ZAP_CHANNEL_CONFIGURED)) {
		if (zchan->span->zio->get_alarms) {
			if ((status = zchan->span->zio->get_alarms(zchan)) == ZAP_SUCCESS) {
				*zchan->last_error = '\0';
				if (zap_test_alarm_flag(zchan, ZAP_ALARM_RED)) {
					snprintf(zchan->last_error + strlen(zchan->last_error), sizeof(zchan->last_error) - strlen(zchan->last_error), "RED/");
				}
				if (zap_test_alarm_flag(zchan, ZAP_ALARM_YELLOW)) {
					snprintf(zchan->last_error + strlen(zchan->last_error), sizeof(zchan->last_error) - strlen(zchan->last_error), "YELLOW/");
				}
				if (zap_test_alarm_flag(zchan, ZAP_ALARM_BLUE)) {
					snprintf(zchan->last_error + strlen(zchan->last_error), sizeof(zchan->last_error) - strlen(zchan->last_error), "BLUE/");
				}
				if (zap_test_alarm_flag(zchan, ZAP_ALARM_LOOPBACK)) {
					snprintf(zchan->last_error + strlen(zchan->last_error), sizeof(zchan->last_error) - strlen(zchan->last_error), "LOOP/");
				}
				if (zap_test_alarm_flag(zchan, ZAP_ALARM_RECOVER)) {
					snprintf(zchan->last_error + strlen(zchan->last_error), sizeof(zchan->last_error) - strlen(zchan->last_error), "RECOVER/");
				}
				*(zchan->last_error + strlen(zchan->last_error) - 1) = '\0';

			}
		} else {
			status = ZAP_NOTIMPL;
		}
	}
	
	return status;
}

zap_status_t zap_span_create(zap_io_interface_t *zio, zap_span_t **span)
{
	zap_span_t *new_span = NULL;
	zap_status_t status = ZAP_FAIL;

	assert(zio != NULL);

	zap_mutex_lock(globals.mutex);
	if (globals.span_index < ZAP_MAX_SPANS_INTERFACE) {
		new_span = globals.spans[++globals.span_index];
		if (!new_span) {
			if (!(new_span = malloc(sizeof(*new_span)))) {
				goto done;
			}
			globals.spans[globals.span_index] = new_span;
		}
		memset(new_span, 0, sizeof(*new_span));
		status = zap_mutex_create(&new_span->mutex);
		if (status != ZAP_SUCCESS) {
			goto done;
		}
		zap_set_flag(new_span, ZAP_SPAN_CONFIGURED);
		new_span->span_id = globals.span_index;
		new_span->zio = zio;
		zap_copy_string(new_span->tone_map[ZAP_TONEMAP_DIAL], "%(1000,0,350,440)", ZAP_TONEMAP_LEN);
		zap_copy_string(new_span->tone_map[ZAP_TONEMAP_RING], "%(2000,4000,440,480)", ZAP_TONEMAP_LEN);
		zap_copy_string(new_span->tone_map[ZAP_TONEMAP_BUSY], "%(500,500,480,620)", ZAP_TONEMAP_LEN);
		zap_copy_string(new_span->tone_map[ZAP_TONEMAP_ATTN], "%(100,100,1400,2060,2450,2600)", ZAP_TONEMAP_LEN);
		new_span->trunk_type = ZAP_TRUNK_NONE;
		new_span->data_type = ZAP_TYPE_SPAN;
		*span = new_span;
		status = ZAP_SUCCESS;
	}

done:
	zap_mutex_unlock(globals.mutex);
	return status;
}

zap_status_t zap_span_close_all(void)
{
	zap_span_t *span;
	uint32_t i, j;

	zap_mutex_lock(globals.mutex);
	for(i = 1; i <= globals.span_index; i++) {
		span = globals.spans[i];
		if (zap_test_flag(span, ZAP_SPAN_CONFIGURED)) {
			for(j = 1; j <= span->chan_count && span->channels[j]; j++) {
				zap_channel_destroy(span->channels[j]);
			}
		} 
	}
	zap_mutex_unlock(globals.mutex);

	return i ? ZAP_SUCCESS : ZAP_FAIL;
}

zap_status_t zap_span_load_tones(zap_span_t *span, const char *mapname)
{
	zap_config_t cfg;
	char *var, *val;
	int x = 0;

	if (!zap_config_open_file(&cfg, "tones.conf")) {
		snprintf(span->last_error, sizeof(span->last_error), "error loading tones.");
		return ZAP_FAIL;
	}
	
	while (zap_config_next_pair(&cfg, &var, &val)) {
		int detect = 0;

		if (!strcasecmp(cfg.category, mapname) && var && val) {
			uint32_t index;
			char *name = NULL;

			if (!strncasecmp(var, "detect-", 7)) {
				name = var + 7;
				detect = 1;
			} else if (!strncasecmp(var, "generate-", 9)) {
				name = var + 9;
			} else {
				zap_log(ZAP_LOG_WARNING, "Unknown tone name %s\n", var);
				continue;
			}

			index = zap_str2zap_tonemap(name);

			if (index >= ZAP_TONEMAP_INVALID || index == ZAP_TONEMAP_NONE) {
				zap_log(ZAP_LOG_WARNING, "Unknown tone name %s\n", name);
			} else {
				if (detect) {
					char *p = val, *next;
					int i = 0;
					do {
						teletone_process_t this;
						next = strchr(p, ',');
						this = (teletone_process_t)atof(p);
						span->tone_detect_map[index].freqs[i++] = this;
						if (next) {
							p = next + 1;
						}
					} while (next);
					zap_log(ZAP_LOG_DEBUG, "added tone detect [%s] = [%s]\n", name, val);
				}  else {
					zap_log(ZAP_LOG_DEBUG, "added tone generation [%s] = [%s]\n", name, val);
					zap_copy_string(span->tone_map[index], val, sizeof(span->tone_map[index]));
				}
				x++;
			}
		}
	}

	zap_config_close_file(&cfg);
	
	if (!x) {
		snprintf(span->last_error, sizeof(span->last_error), "error loading tones.");
		return ZAP_FAIL;
	}

	return ZAP_SUCCESS;
	
}

zap_status_t zap_span_add_channel(zap_span_t *span, zap_socket_t sockfd, zap_chan_type_t type, zap_channel_t **chan)
{
	if (span->chan_count < ZAP_MAX_CHANNELS_SPAN) {
		zap_channel_t *new_chan = span->channels[++span->chan_count];

		if (!new_chan) {
			if (!(new_chan = malloc(sizeof(*new_chan)))) {
				return ZAP_FAIL;
			}
			span->channels[span->chan_count] = new_chan;
			memset(new_chan, 0, sizeof(*new_chan));
		}

		new_chan->type = type;
		new_chan->sockfd = sockfd;
		new_chan->zio = span->zio;
		new_chan->span_id = span->span_id;
		new_chan->chan_id = span->chan_count;
		new_chan->span = span;
		new_chan->fds[0] = -1;
		new_chan->fds[1] = -1;
		new_chan->data_type = ZAP_TYPE_CHANNEL;
		if (!new_chan->dtmf_on) {
			new_chan->dtmf_on = ZAP_DEFAULT_DTMF_ON;
		}

		if (!new_chan->dtmf_off) {
			new_chan->dtmf_off = ZAP_DEFAULT_DTMF_OFF;
		}

		zap_mutex_create(&new_chan->mutex);
		zap_buffer_create(&new_chan->digit_buffer, 128, 128, 0);
		zap_buffer_create(&new_chan->gen_dtmf_buffer, 128, 128, 0);
		new_chan->variable_hash = create_hashtable(16, zap_hash_hashfromstring, zap_hash_equalkeys);

		new_chan->dtmf_hangup_buf = calloc (span->dtmf_hangup_len + 1, sizeof (char));

		zap_set_flag(new_chan, ZAP_CHANNEL_CONFIGURED | ZAP_CHANNEL_READY);
		*chan = new_chan;
		return ZAP_SUCCESS;
	}

	return ZAP_FAIL;
}

zap_status_t zap_span_find_by_name(const char *name, zap_span_t **span)
{
	zap_status_t status = ZAP_FAIL;

	zap_mutex_lock(globals.mutex);
	if (!zap_strlen_zero(name) && (*span = hashtable_search(globals.span_hash, (void *)name))) {
		status = ZAP_SUCCESS;
	}
	zap_mutex_unlock(globals.mutex);

	return status;
}

zap_status_t zap_span_find(uint32_t id, zap_span_t **span)
{
	zap_span_t *fspan;

	if (id > ZAP_MAX_SPANS_INTERFACE) {
		return ZAP_FAIL;
	}

	zap_mutex_lock(globals.mutex);
	fspan = globals.spans[id];
	zap_mutex_unlock(globals.mutex);

	if (!fspan || !zap_test_flag(fspan, ZAP_SPAN_CONFIGURED)) {
		return ZAP_FAIL;
	}

	*span = fspan;

	return ZAP_SUCCESS;
	
}

zap_status_t zap_span_set_event_callback(zap_span_t *span, zio_event_cb_t event_callback)
{
	zap_mutex_lock(span->mutex);
	span->event_callback = event_callback;
	zap_mutex_unlock(span->mutex);
	return ZAP_SUCCESS;
}


zap_status_t zap_span_poll_event(zap_span_t *span, uint32_t ms)
{
	assert(span->zio != NULL);

	if (span->zio->poll_event) {
		return span->zio->poll_event(span, ms);
	} else {
		zap_log(ZAP_LOG_ERROR, "poll_event method not implemented in module %s!", span->zio->name);
	}

	return ZAP_NOTIMPL;
}

zap_status_t zap_span_next_event(zap_span_t *span, zap_event_t **event)
{
	assert(span->zio != NULL);

	if (span->zio->next_event) {
		return span->zio->next_event(span, event);
	} else {
		zap_log(ZAP_LOG_ERROR, "next_event method not implemented in module %s!", span->zio->name);
	}
	
	return ZAP_NOTIMPL;
}

static zap_status_t zchan_fsk_write_sample(int16_t *buf, zap_size_t buflen, void *user_data)
{
	zap_channel_t *zchan = (zap_channel_t *) user_data;
	zap_buffer_write(zchan->fsk_buffer, buf, buflen * 2);
	return ZAP_SUCCESS;
}

zap_status_t zap_channel_send_fsk_data(zap_channel_t *zchan, zap_fsk_data_state_t *fsk_data, float db_level)
{
	struct zap_fsk_modulator fsk_trans;

	if (!zchan->fsk_buffer) {
		zap_buffer_create(&zchan->fsk_buffer, 128, 128, 0);
	} else {
		zap_buffer_zero(zchan->fsk_buffer);
	}

	if (zchan->token_count > 1) {
		zap_fsk_modulator_init(&fsk_trans, FSK_BELL202, zchan->rate, fsk_data, db_level, 80, 5, 0, zchan_fsk_write_sample, zchan);
		zap_fsk_modulator_send_all((&fsk_trans));
	} else {
		zap_fsk_modulator_init(&fsk_trans, FSK_BELL202, zchan->rate, fsk_data, db_level, 180, 5, 300, zchan_fsk_write_sample, zchan);
		zap_fsk_modulator_send_all((&fsk_trans));
		zchan->buffer_delay = 3500 / zchan->effective_interval;
	}

	return ZAP_SUCCESS;
}


zap_status_t zap_channel_set_event_callback(zap_channel_t *zchan, zio_event_cb_t event_callback)
{
	zap_mutex_lock(zchan->mutex);
	zchan->event_callback = event_callback;
	zap_mutex_unlock(zchan->mutex);
	return ZAP_SUCCESS;
}

zap_status_t zap_channel_clear_token(zap_channel_t *zchan, const char *token)
{
	zap_status_t status = ZAP_FAIL;
	
	zap_mutex_lock(zchan->mutex);
	if (token == NULL) {
		memset(zchan->tokens, 0, sizeof(zchan->tokens));
		zchan->token_count = 0;
	} else if (*token != '\0') {
		char tokens[ZAP_MAX_TOKENS][ZAP_TOKEN_STRLEN];
		int32_t i, count = zchan->token_count;
		memcpy(tokens, zchan->tokens, sizeof(tokens));
		memset(zchan->tokens, 0, sizeof(zchan->tokens));
		zchan->token_count = 0;		

		for (i = 0; i < count; i++) {
			if (strcmp(tokens[i], token)) {
				zap_copy_string(zchan->tokens[zchan->token_count], tokens[i], sizeof(zchan->tokens[zchan->token_count]));
				zchan->token_count++;
			}
		}

		status = ZAP_SUCCESS;
	}
	zap_mutex_unlock(zchan->mutex);

	return status;
}

void zap_channel_rotate_tokens(zap_channel_t *zchan)
{
	if (zchan->token_count) {
		memmove(zchan->tokens[1], zchan->tokens[0], zchan->token_count * ZAP_TOKEN_STRLEN);
		zap_copy_string(zchan->tokens[0], zchan->tokens[zchan->token_count], ZAP_TOKEN_STRLEN);
		*zchan->tokens[zchan->token_count] = '\0';
	}
}

zap_status_t zap_channel_add_token(zap_channel_t *zchan, char *token, int end)
{
	zap_status_t status = ZAP_FAIL;

	zap_mutex_lock(zchan->mutex);
	if (zchan->token_count < ZAP_MAX_TOKENS) {
		if (end) {
			zap_copy_string(zchan->tokens[zchan->token_count++], token, ZAP_TOKEN_STRLEN);
		} else {
			memmove(zchan->tokens[1], zchan->tokens[0], zchan->token_count * ZAP_TOKEN_STRLEN);
			zap_copy_string(zchan->tokens[0], token, ZAP_TOKEN_STRLEN);
			zchan->token_count++;
		}
		status = ZAP_SUCCESS;
	}
	zap_mutex_unlock(zchan->mutex);

	return status;
}


zap_status_t zap_channel_complete_state(zap_channel_t *zchan)
{
	zap_channel_state_t state = zchan->state;

	if (state == ZAP_CHANNEL_STATE_PROGRESS) {
		zap_set_flag(zchan, ZAP_CHANNEL_PROGRESS);
	} else if (state == ZAP_CHANNEL_STATE_UP) {
		zap_set_flag(zchan, ZAP_CHANNEL_PROGRESS);
		zap_set_flag(zchan, ZAP_CHANNEL_MEDIA);	
		zap_set_flag(zchan, ZAP_CHANNEL_ANSWERED);	
	} else if (state == ZAP_CHANNEL_STATE_PROGRESS_MEDIA) {
		zap_set_flag(zchan, ZAP_CHANNEL_PROGRESS);	
		zap_set_flag(zchan, ZAP_CHANNEL_MEDIA);	
	}

	return ZAP_SUCCESS;
}

static int zap_parse_state_map(zap_channel_t *zchan, zap_channel_state_t state, zap_state_map_t *state_map)
{
	int x = 0, ok = 0;
	zap_state_direction_t direction = zap_test_flag(zchan, ZAP_CHANNEL_OUTBOUND) ? ZSD_OUTBOUND : ZSD_INBOUND;

	for(x = 0; x < ZAP_MAP_NODE_SIZE; x++) {
		int i = 0, proceed = 0;
		if (!state_map->nodes[x].type) {
			break;
		}

		if (state_map->nodes[x].direction != direction) {
			continue;
		}
		
		if (state_map->nodes[x].check_states[0] == ZAP_ANY_STATE) {
			proceed = 1;
		} else {
			for(i = 0; i < ZAP_MAP_MAX; i++) {
				if (state_map->nodes[x].check_states[i] == zchan->state) {
					proceed = 1;
					break;
				}
			}
		}

		if (!proceed) {
			continue;
		}
		
		for(i = 0; i < ZAP_MAP_MAX; i++) {
			ok = (state_map->nodes[x].type == ZSM_ACCEPTABLE);
			if (state_map->nodes[x].states[i] == ZAP_END) {
				break;
			}
			if (state_map->nodes[x].states[i] == state) {
				ok = !ok;
				goto end;
			}
		}
	}
 end:
	
	return ok;
}

zap_status_t zap_channel_set_state(zap_channel_t *zchan, zap_channel_state_t state, int lock)
{
	int ok = 1;
	
	if (!zap_test_flag(zchan, ZAP_CHANNEL_READY)) {
		return ZAP_FAIL;
	}

	if (zap_test_flag(zchan->span, ZAP_SPAN_SUSPENDED)) {
		if (state != ZAP_CHANNEL_STATE_RESTART && state != ZAP_CHANNEL_STATE_DOWN) {
			return ZAP_FAIL;
		}
	}

	if (lock) {
		zap_mutex_lock(zchan->mutex);
	}

	if (zchan->span->state_map) {
		ok = zap_parse_state_map(zchan, state, zchan->span->state_map);
		goto end;
	}

	switch(zchan->state) {
	case ZAP_CHANNEL_STATE_HANGUP:
	case ZAP_CHANNEL_STATE_TERMINATING:
		{
			ok = 0;
			switch(state) {
			case ZAP_CHANNEL_STATE_DOWN:
			case ZAP_CHANNEL_STATE_BUSY:
			case ZAP_CHANNEL_STATE_RESTART:
				ok = 1;
				break;
			default:
				break;
			}
		}
		break;
	case ZAP_CHANNEL_STATE_UP:
		{
			ok = 1;
			switch(state) {
			case ZAP_CHANNEL_STATE_PROGRESS:
			case ZAP_CHANNEL_STATE_PROGRESS_MEDIA:
			case ZAP_CHANNEL_STATE_RING:
				ok = 0;
				break;
			default:
				break;
			}
		}
		break;
	case ZAP_CHANNEL_STATE_DOWN:
		{
			ok = 0;
			
			switch(state) {
			case ZAP_CHANNEL_STATE_DIALTONE:
			case ZAP_CHANNEL_STATE_COLLECT:
			case ZAP_CHANNEL_STATE_DIALING:
			case ZAP_CHANNEL_STATE_RING:
			case ZAP_CHANNEL_STATE_PROGRESS_MEDIA:
			case ZAP_CHANNEL_STATE_PROGRESS:				
			case ZAP_CHANNEL_STATE_GET_CALLERID:
			case ZAP_CHANNEL_STATE_GENRING:
				ok = 1;
				break;
			default:
				break;
			}
		}
		break;
	case ZAP_CHANNEL_STATE_BUSY:
		{
			switch(state) {
			case ZAP_CHANNEL_STATE_UP:
				ok = 0;
				break;
			default:
				break;
			}
		}
		break;
	case ZAP_CHANNEL_STATE_RING:
		{
			switch(state) {
			case ZAP_CHANNEL_STATE_UP:
				ok = 1;
				break;
			default:
				break;
			}
		}
		break;
	default:
		break;
	}

 end:

	if (state == zchan->state) {
		ok = 0;
	}
	

	if (ok) {
		if (zchan->state == ZAP_CHANNEL_STATE_DOWN) {
			zchan->span->active_count++;
		} else if (state == ZAP_CHANNEL_STATE_DOWN) {
			zchan->span->active_count--;
		}

		zap_set_flag(zchan, ZAP_CHANNEL_STATE_CHANGE);	
		zap_set_flag_locked(zchan->span, ZAP_SPAN_STATE_CHANGE);	
		zchan->last_state = zchan->state; 
		zchan->state = state;
	}

	if (lock) {
		zap_mutex_unlock(zchan->mutex);
	}

	return ok ? ZAP_SUCCESS : ZAP_FAIL;
}

zap_status_t zap_channel_open_any(uint32_t span_id, zap_direction_t direction, zap_caller_data_t *caller_data, zap_channel_t **zchan)
{
	zap_status_t status = ZAP_FAIL;
	zap_channel_t *check;
	uint32_t i,j;
	zap_span_t *span;
	uint32_t span_max;

	if (span_id) {
		if (span_id > globals.span_index || !globals.spans[span_id]) {
			zap_log(ZAP_LOG_CRIT, "SPAN NOT DEFINED!\n");
			*zchan = NULL;
            return ZAP_FAIL;
		}

		if (globals.spans[span_id]->active_count >= globals.spans[span_id]->chan_count) {
			zap_log(ZAP_LOG_CRIT, "All circuits are busy.\n");
			*zchan = NULL;
			return ZAP_FAIL;
		}
		
		if (globals.spans[span_id]->channel_request && !globals.spans[span_id]->suggest_chan_id) {
			return globals.spans[span_id]->channel_request(globals.spans[span_id], 0, direction, caller_data, zchan);
		}
		
		span_max = span_id;
		j = span_id;
	} else {
		span_max = globals.span_index;
		if (direction == ZAP_TOP_DOWN) {
			j = 1;
		} else {
			j = span_max;
		}
	}
	
	for(;;) {
		if (direction == ZAP_TOP_DOWN) {
			if (j > span_max) {
				goto done;
			}
		} else {
			if (j == 0) {
				goto done;
			}
		}

		span = globals.spans[j];
		zap_mutex_lock(span->mutex);

		if (!zap_test_flag(span, ZAP_SPAN_CONFIGURED)) {
			goto next_loop;
		}
		
		if (direction == ZAP_TOP_DOWN) {
			i = 1;
		} else {
			i = span->chan_count;
		}	
		
		for(;;) {

			if (direction == ZAP_TOP_DOWN) {
				if (i > span->chan_count) {
					break;
				}
			} else {
				if (i == 0) {
					break;
				}
			}
			
			if (!(check = span->channels[i])) {
				status = ZAP_FAIL;
				zap_mutex_unlock(span->mutex);
				goto done;
			}
			
			if (zap_test_flag(check, ZAP_CHANNEL_READY) && 
				!zap_test_flag(check, ZAP_CHANNEL_INUSE) && 
				!zap_test_flag(check, ZAP_CHANNEL_SUSPENDED) && 
				check->state == ZAP_CHANNEL_STATE_DOWN
				) {

				if (globals.spans[span_id]->channel_request) {
					status = globals.spans[span_id]->channel_request(globals.spans[span_id], i, direction, caller_data, zchan);
					zap_mutex_unlock(span->mutex);
                    goto done;
				}

				status = check->zio->open(check);
				
				if (status == ZAP_SUCCESS) {
					zap_set_flag(check, ZAP_CHANNEL_INUSE);
					zap_channel_open_chan(check);
					*zchan = check;
					zap_mutex_unlock(span->mutex);
					goto done;
				}
			}
			
			if (direction == ZAP_TOP_DOWN) {
				i++;
			} else {
				i--;
			}
		}
		
	next_loop:
		
		zap_mutex_unlock(span->mutex);
		
		if (direction == ZAP_TOP_DOWN) {
			j++;
		} else {
			j--;
		}
	}

 done:

	return status;
}

static zap_status_t zap_channel_reset(zap_channel_t *zchan)
{
	zap_clear_flag(zchan, ZAP_CHANNEL_OPEN);
	zchan->event_callback = NULL;
	zap_clear_flag(zchan, ZAP_CHANNEL_DTMF_DETECT);
	zap_clear_flag(zchan, ZAP_CHANNEL_SUPRESS_DTMF);
	zap_channel_done(zchan);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_HOLD);

	memset(zchan->tokens, 0, sizeof(zchan->tokens));
	zchan->token_count = 0;

	if (zchan->dtmf_buffer) {
		zap_buffer_zero(zchan->dtmf_buffer);
	}

	if (zchan->gen_dtmf_buffer) {
		zap_buffer_zero(zchan->gen_dtmf_buffer);
	}

	if (zchan->digit_buffer) {
		zap_buffer_zero(zchan->digit_buffer);
	}

	if (!zchan->dtmf_on) {
		zchan->dtmf_on = ZAP_DEFAULT_DTMF_ON;
	}

	if (!zchan->dtmf_off) {
		zchan->dtmf_off = ZAP_DEFAULT_DTMF_OFF;
	}
	
	memset(zchan->dtmf_hangup_buf, '\0', zchan->span->dtmf_hangup_len);

	if (zap_test_flag(zchan, ZAP_CHANNEL_TRANSCODE)) {
		zchan->effective_codec = zchan->native_codec;
		zchan->packet_len = zchan->native_interval * (zchan->effective_codec == ZAP_CODEC_SLIN ? 16 : 8);
		zap_clear_flag(zchan, ZAP_CHANNEL_TRANSCODE);
	}

	return ZAP_SUCCESS;
}

zap_status_t zap_channel_init(zap_channel_t *zchan)
{

	if (zchan->init_state != ZAP_CHANNEL_STATE_DOWN) {
		zap_set_state_locked(zchan, zchan->init_state);
		zchan->init_state = ZAP_CHANNEL_STATE_DOWN;
	}

	return ZAP_SUCCESS;
}

zap_status_t zap_channel_open_chan(zap_channel_t *zchan)
{
	zap_status_t status = ZAP_FAIL;

	assert(zchan != NULL);

	if (zap_test_flag(zchan, ZAP_CHANNEL_SUSPENDED)) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "%s", "Channel is suspended");
		return ZAP_FAIL;
	}
	
	if (!zap_test_flag(zchan, ZAP_CHANNEL_READY) || (status = zap_mutex_trylock(zchan->mutex)) != ZAP_SUCCESS) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "Channel is not ready or is in use %d %d", zap_test_flag(zchan, ZAP_CHANNEL_READY), status);
		return status;
	}

	status = ZAP_FAIL;

	if (zap_test_flag(zchan, ZAP_CHANNEL_READY)) {
		status = zchan->span->zio->open(zchan);
		if (status == ZAP_SUCCESS) {
			zap_set_flag(zchan, ZAP_CHANNEL_OPEN | ZAP_CHANNEL_INUSE);
		}
	} else {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "%s", "Channel is not ready");
	}

	zap_mutex_unlock(zchan->mutex);
	return status;
}

zap_status_t zap_channel_open(uint32_t span_id, uint32_t chan_id, zap_channel_t **zchan)
{
	zap_channel_t *check;
	zap_status_t status = ZAP_FAIL;

	zap_mutex_lock(globals.mutex);

	if (span_id > globals.span_index  || chan_id >= ZAP_MAX_CHANNELS_SPAN || !globals.spans[span_id]) {
		zap_log(ZAP_LOG_ERROR, "SPAN NOT DEFINED!\n");
		*zchan = NULL;
		goto done;		
	}

	if (globals.spans[span_id]->channel_request) {
		zap_log(ZAP_LOG_ERROR, "Individual channel selection not implemented on this span.\n");
		*zchan = NULL;
		goto done;
	}
	
	check = globals.spans[span_id]->channels[chan_id];

	if (zap_test_flag(check, ZAP_CHANNEL_SUSPENDED) || 
		!zap_test_flag(check, ZAP_CHANNEL_READY) || (status = zap_mutex_trylock(check->mutex)) != ZAP_SUCCESS) {
		*zchan = NULL;
		goto done;
	}
	
	status = ZAP_FAIL;

	if (zap_test_flag(check, ZAP_CHANNEL_READY) && (!zap_test_flag(check, ZAP_CHANNEL_INUSE) || 
													(check->type == ZAP_CHAN_TYPE_FXS && check->token_count == 1))) {
		if (!zap_test_flag(check, ZAP_CHANNEL_OPEN)) {
			status = check->zio->open(check);
			if (status == ZAP_SUCCESS) {
				zap_set_flag(check, ZAP_CHANNEL_OPEN);
			}
		} else {
			status = ZAP_SUCCESS;
		}
		zap_set_flag(check, ZAP_CHANNEL_INUSE);
		*zchan = check;
	}
	zap_mutex_unlock(check->mutex);

	done:
	zap_mutex_unlock(globals.mutex);

	return status;
}

zap_status_t zap_channel_outgoing_call(zap_channel_t *zchan)
{
	zap_status_t status;

	assert(zchan != NULL);
	
	if (zchan->span->outgoing_call) {
		if ((status = zchan->span->outgoing_call(zchan)) == ZAP_SUCCESS) {
			zap_set_flag(zchan, ZAP_CHANNEL_OUTBOUND);
		}
		return status;
	} else {
		zap_log(ZAP_LOG_ERROR, "outgoing_call method not implemented!\n");
	}
	
	return ZAP_FAIL;
}

zap_status_t zap_channel_done(zap_channel_t *zchan)
{
	assert(zchan != NULL);

	memset(&zchan->caller_data, 0, sizeof(zchan->caller_data));

	zap_clear_flag_locked(zchan, ZAP_CHANNEL_INUSE);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_OUTBOUND);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_WINK);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_FLASH);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_STATE_CHANGE);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_HOLD);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_OFFHOOK);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_RINGING);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_PROGRESS_DETECT);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_CALLERID_DETECT);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_3WAY);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_PROGRESS);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_MEDIA);
	zap_clear_flag_locked(zchan, ZAP_CHANNEL_ANSWERED);
	zchan->init_state = ZAP_CHANNEL_STATE_DOWN;
	zchan->state = ZAP_CHANNEL_STATE_DOWN;
	zap_log(ZAP_LOG_DEBUG, "channel done %u:%u\n", zchan->span_id, zchan->chan_id);

	return ZAP_SUCCESS;
}

zap_status_t zap_channel_use(zap_channel_t *zchan)
{

	assert(zchan != NULL);

	zap_set_flag_locked(zchan, ZAP_CHANNEL_INUSE);

	return ZAP_SUCCESS;
}

zap_status_t zap_channel_close(zap_channel_t **zchan)
{
	zap_channel_t *check;
	zap_status_t status = ZAP_FAIL;

	assert(zchan != NULL);
	check = *zchan;
	*zchan = NULL;

	if (!check) {
		return ZAP_FAIL;
	}

	if (zap_test_flag(check, ZAP_CHANNEL_CONFIGURED)) {
		zap_mutex_lock(check->mutex);
		if (zap_test_flag(check, ZAP_CHANNEL_OPEN)) {
			status = check->zio->close(check);
			if (status == ZAP_SUCCESS) {
				zap_channel_reset(check);
				*zchan = NULL;
			}
		}
		check->ring_count = 0;
		zap_mutex_unlock(check->mutex);
	}
	
	return status;
}


static zap_status_t zchan_activate_dtmf_buffer(zap_channel_t *zchan)
{

	if (!zchan->dtmf_buffer) {
		if (zap_buffer_create(&zchan->dtmf_buffer, 1024, 3192, 0) != ZAP_SUCCESS) {
			zap_log(ZAP_LOG_ERROR, "Failed to allocate DTMF Buffer!\n");
			snprintf(zchan->last_error, sizeof(zchan->last_error), "buffer error");
			return ZAP_FAIL;
		} else {
			zap_log(ZAP_LOG_DEBUG, "Created DTMF Buffer!\n");
		}
	}

	
	if (!zchan->tone_session.buffer) {
		memset(&zchan->tone_session, 0, sizeof(zchan->tone_session));
		teletone_init_session(&zchan->tone_session, 0, NULL, NULL);
	}

	zchan->tone_session.rate = zchan->rate;
	zchan->tone_session.duration = zchan->dtmf_on * (zchan->tone_session.rate / 1000);
	zchan->tone_session.wait = zchan->dtmf_off * (zchan->tone_session.rate / 1000);
	zchan->tone_session.volume = -7;

	/*
	  zchan->tone_session.debug = 1;
	  zchan->tone_session.debug_stream = stdout;
	*/

	return ZAP_SUCCESS;
}

zap_status_t zap_channel_command(zap_channel_t *zchan, zap_command_t command, void *obj)
{
	zap_status_t status = ZAP_FAIL;
	
	assert(zchan != NULL);
	assert(zchan->zio != NULL);

	zap_mutex_lock(zchan->mutex);

	switch(command) {

	case ZAP_COMMAND_ENABLE_CALLERID_DETECT:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_CALLERID)) {
				if (zap_fsk_demod_init(&zchan->fsk, zchan->rate, zchan->fsk_buf, sizeof(zchan->fsk_buf)) != ZAP_SUCCESS) {
					snprintf(zchan->last_error, sizeof(zchan->last_error), "%s", strerror(errno));
					GOTO_STATUS(done, ZAP_FAIL);
				}
				zap_set_flag_locked(zchan, ZAP_CHANNEL_CALLERID_DETECT);
			}
		}
		break;
	case ZAP_COMMAND_DISABLE_CALLERID_DETECT:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_CALLERID)) {
				zap_fsk_demod_destroy(&zchan->fsk);
				zap_clear_flag_locked(zchan, ZAP_CHANNEL_CALLERID_DETECT);
			}
		}
		break;
	case ZAP_COMMAND_TRACE_INPUT:
		{
			char *path = (char *) obj;
			if (zchan->fds[0] > 0) {
				close(zchan->fds[0]);
				zchan->fds[0] = -1;
			}
			if ((zchan->fds[0] = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) > -1) {
				zap_log(ZAP_LOG_DEBUG, "Tracing channel %u:%u to [%s]\n", zchan->span_id, zchan->chan_id, path);	
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
			
			snprintf(zchan->last_error, sizeof(zchan->last_error), "%s", strerror(errno));
			GOTO_STATUS(done, ZAP_FAIL);
		}
		break;
	case ZAP_COMMAND_TRACE_OUTPUT:
		{
			char *path = (char *) obj;
			if (zchan->fds[1] > 0) {
				close(zchan->fds[1]);
				zchan->fds[1] = -1;
			}
			if ((zchan->fds[1] = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) > -1) {
				zap_log(ZAP_LOG_DEBUG, "Tracing channel %u:%u to [%s]\n", zchan->span_id, zchan->chan_id, path);	
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
			
			snprintf(zchan->last_error, sizeof(zchan->last_error), "%s", strerror(errno));
			GOTO_STATUS(done, ZAP_FAIL);
		}
		break;
	case ZAP_COMMAND_SET_INTERVAL:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_INTERVAL)) {
				zchan->effective_interval = ZAP_COMMAND_OBJ_INT;
				if (zchan->effective_interval == zchan->native_interval) {
					zap_clear_flag(zchan, ZAP_CHANNEL_BUFFER);
				} else {
					zap_set_flag(zchan, ZAP_CHANNEL_BUFFER);
				}
				zchan->packet_len = zchan->native_interval * (zchan->effective_codec == ZAP_CODEC_SLIN ? 16 : 8);
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_GET_INTERVAL:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_INTERVAL)) {
				ZAP_COMMAND_OBJ_INT = zchan->effective_interval;
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_SET_CODEC:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_CODECS)) {
				zchan->effective_codec = ZAP_COMMAND_OBJ_INT;
				
				if (zchan->effective_codec == zchan->native_codec) {
					zap_clear_flag(zchan, ZAP_CHANNEL_TRANSCODE);
				} else {
					zap_set_flag(zchan, ZAP_CHANNEL_TRANSCODE);
				}
				zchan->packet_len = zchan->native_interval * (zchan->effective_codec == ZAP_CODEC_SLIN ? 16 : 8);
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;

	case ZAP_COMMAND_SET_NATIVE_CODEC:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_CODECS)) {
				zchan->effective_codec = zchan->native_codec;
				zap_clear_flag(zchan, ZAP_CHANNEL_TRANSCODE);
				zchan->packet_len = zchan->native_interval * (zchan->effective_codec == ZAP_CODEC_SLIN ? 16 : 8);
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;

	case ZAP_COMMAND_GET_CODEC: 
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_CODECS)) {
				ZAP_COMMAND_OBJ_INT = zchan->effective_codec;
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_GET_NATIVE_CODEC: 
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_CODECS)) {
				ZAP_COMMAND_OBJ_INT = zchan->native_codec;
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_ENABLE_PROGRESS_DETECT:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_PROGRESS)) {
				/* if they don't have thier own, use ours */
				zap_channel_clear_detected_tones(zchan);
				zap_channel_clear_needed_tones(zchan);
				teletone_multi_tone_init(&zchan->span->tone_finder[ZAP_TONEMAP_DIAL], &zchan->span->tone_detect_map[ZAP_TONEMAP_DIAL]);
				teletone_multi_tone_init(&zchan->span->tone_finder[ZAP_TONEMAP_RING], &zchan->span->tone_detect_map[ZAP_TONEMAP_RING]);
				teletone_multi_tone_init(&zchan->span->tone_finder[ZAP_TONEMAP_BUSY], &zchan->span->tone_detect_map[ZAP_TONEMAP_BUSY]);
				zap_set_flag(zchan, ZAP_CHANNEL_PROGRESS_DETECT);
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_DISABLE_PROGRESS_DETECT:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_PROGRESS)) {
				zap_clear_flag_locked(zchan, ZAP_CHANNEL_PROGRESS_DETECT);
				zap_channel_clear_detected_tones(zchan);
				zap_channel_clear_needed_tones(zchan);
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_ENABLE_DTMF_DETECT:
		{
			/* if they don't have thier own, use ours */
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_DETECT)) {
				zap_tone_type_t tt = ZAP_COMMAND_OBJ_INT;
				if (tt == ZAP_TONE_DTMF) {
					teletone_dtmf_detect_init (&zchan->dtmf_detect, zchan->rate);
					zap_set_flag_locked(zchan, ZAP_CHANNEL_DTMF_DETECT);
					zap_set_flag_locked(zchan, ZAP_CHANNEL_SUPRESS_DTMF);
					GOTO_STATUS(done, ZAP_SUCCESS);
				} else {
					snprintf(zchan->last_error, sizeof(zchan->last_error), "invalid command");
					GOTO_STATUS(done, ZAP_FAIL);
				}
			}
		}
		break;
	case ZAP_COMMAND_DISABLE_DTMF_DETECT:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_DETECT)) {
				zap_tone_type_t tt = ZAP_COMMAND_OBJ_INT;
                if (tt == ZAP_TONE_DTMF) {
                    teletone_dtmf_detect_init (&zchan->dtmf_detect, zchan->rate);
                    zap_clear_flag(zchan, ZAP_CHANNEL_DTMF_DETECT);
					zap_clear_flag(zchan, ZAP_CHANNEL_SUPRESS_DTMF);
					GOTO_STATUS(done, ZAP_SUCCESS);
                } else {
                    snprintf(zchan->last_error, sizeof(zchan->last_error), "invalid command");
					GOTO_STATUS(done, ZAP_FAIL);
                }
			}
		}
	case ZAP_COMMAND_GET_DTMF_ON_PERIOD:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_GENERATE)) {
				ZAP_COMMAND_OBJ_INT = zchan->dtmf_on;
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_GET_DTMF_OFF_PERIOD:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_GENERATE)) {
				ZAP_COMMAND_OBJ_INT = zchan->dtmf_on;
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	case ZAP_COMMAND_SET_DTMF_ON_PERIOD:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_GENERATE)) {
				int val = ZAP_COMMAND_OBJ_INT;
				if (val > 10 && val < 1000) {
					zchan->dtmf_on = val;
					GOTO_STATUS(done, ZAP_SUCCESS);
				} else {
					snprintf(zchan->last_error, sizeof(zchan->last_error), "invalid value %d range 10-1000", val);
					GOTO_STATUS(done, ZAP_FAIL);
				}
			}
		}
		break;
	case ZAP_COMMAND_SET_DTMF_OFF_PERIOD:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_GENERATE)) {
				int val = ZAP_COMMAND_OBJ_INT;
				if (val > 10 && val < 1000) {
					zchan->dtmf_off = val;
					GOTO_STATUS(done, ZAP_SUCCESS);
				} else {
					snprintf(zchan->last_error, sizeof(zchan->last_error), "invalid value %d range 10-1000", val);
					GOTO_STATUS(done, ZAP_FAIL);
				}
			}
		}
		break;
	case ZAP_COMMAND_SEND_DTMF:
		{
			if (!zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_GENERATE)) {
				char *digits = ZAP_COMMAND_OBJ_CHAR_P;
				
				if ((status = zchan_activate_dtmf_buffer(zchan)) != ZAP_SUCCESS) {
					GOTO_STATUS(done, status);
				}
				
				zap_buffer_write(zchan->gen_dtmf_buffer, digits, strlen(digits));
				
				GOTO_STATUS(done, ZAP_SUCCESS);
			}
		}
		break;
	default:
		break;
	}

	if (!zchan->zio->command) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "method not implemented");
		zap_log(ZAP_LOG_ERROR, "no commnand functon!\n");	
		GOTO_STATUS(done, ZAP_FAIL);
	}

    status = zchan->zio->command(zchan, command, obj);


 done:
	zap_mutex_unlock(zchan->mutex);
	return status;

}

zap_status_t zap_channel_wait(zap_channel_t *zchan, zap_wait_flag_t *flags, int32_t to)
{
	assert(zchan != NULL);
	assert(zchan->zio != NULL);

    if (!zap_test_flag(zchan, ZAP_CHANNEL_OPEN)) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "channel not open");
        return ZAP_FAIL;
    }

	if (!zchan->zio->wait) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "method not implemented");
		return ZAP_FAIL;
	}

    return zchan->zio->wait(zchan, flags, to);

}

/*******************************/
ZIO_CODEC_FUNCTION(zio_slin2ulaw)
{
	int16_t sln_buf[512] = {0}, *sln = sln_buf;
	uint8_t *lp = data;
	uint32_t i;
	zap_size_t len = *datalen;

	if (max > len) {
		max = len;
	}

	memcpy(sln, data, max);
	
	for(i = 0; i < max; i++) {
		*lp++ = linear_to_ulaw(*sln++);
	}

	*datalen = max / 2;

	return ZAP_SUCCESS;

}


ZIO_CODEC_FUNCTION(zio_ulaw2slin)
{
	int16_t *sln = data;
	uint8_t law[1024] = {0}, *lp = law;
	uint32_t i;
	zap_size_t len = *datalen;
	
	if (max > len) {
		max = len;
	}

	memcpy(law, data, max);

	for(i = 0; i < max; i++) {
		*sln++ = ulaw_to_linear(*lp++);
	}
	
	*datalen = max * 2;

	return ZAP_SUCCESS;
}

ZIO_CODEC_FUNCTION(zio_slin2alaw)
{
	int16_t sln_buf[512] = {0}, *sln = sln_buf;
	uint8_t *lp = data;
	uint32_t i;
	zap_size_t len = *datalen;

	if (max > len) {
		max = len;
	}

	memcpy(sln, data, max);
	
	for(i = 0; i < max; i++) {
		*lp++ = linear_to_alaw(*sln++);
	}

	*datalen = max / 2;

	return ZAP_SUCCESS;

}


ZIO_CODEC_FUNCTION(zio_alaw2slin)
{
	int16_t *sln = data;
	uint8_t law[1024] = {0}, *lp = law;
	uint32_t i;
	zap_size_t len = *datalen;
	
	if (max > len) {
		max = len;
	}

	memcpy(law, data, max);

	for(i = 0; i < max; i++) {
		*sln++ = alaw_to_linear(*lp++);
	}

	*datalen = max * 2;

	return ZAP_SUCCESS;
}

ZIO_CODEC_FUNCTION(zio_ulaw2alaw)
{
	zap_size_t len = *datalen;
	uint32_t i;
	uint8_t *lp = data;

	if (max > len) {
        max = len;
    }

	for(i = 0; i < max; i++) {
		*lp = ulaw_to_alaw(*lp);
		lp++;
	}

	return ZAP_SUCCESS;
}

ZIO_CODEC_FUNCTION(zio_alaw2ulaw)
{
	zap_size_t len = *datalen;
	uint32_t i;
	uint8_t *lp = data;

	if (max > len) {
        max = len;
    }

	for(i = 0; i < max; i++) {
		*lp = alaw_to_ulaw(*lp);
		lp++;
	}

	return ZAP_SUCCESS;
}

/******************************/

void zap_channel_clear_detected_tones(zap_channel_t *zchan)
{
	memset(zchan->detected_tones, 0, sizeof(zchan->detected_tones[0]) * ZAP_TONEMAP_INVALID);
}

void zap_channel_clear_needed_tones(zap_channel_t *zchan)
{
	memset(zchan->needed_tones, 0, sizeof(zchan->needed_tones[0]) * ZAP_TONEMAP_INVALID);
}

zap_size_t zap_channel_dequeue_dtmf(zap_channel_t *zchan, char *dtmf, zap_size_t len)
{
	zap_size_t bytes = 0;

	assert(zchan != NULL);

	if (!zap_test_flag(zchan, ZAP_CHANNEL_READY)) {
		return ZAP_FAIL;
	}

	if (zchan->digit_buffer && zap_buffer_inuse(zchan->digit_buffer)) {
		zap_mutex_lock(zchan->mutex);
		if ((bytes = zap_buffer_read(zchan->digit_buffer, dtmf, len)) > 0) {
			*(dtmf + bytes) = '\0';
		}
		zap_mutex_unlock(zchan->mutex);
	}

	return bytes;
}

void zap_channel_flush_dtmf(zap_channel_t *zchan)
{
	if (zchan->digit_buffer && zap_buffer_inuse(zchan->digit_buffer)) {
		zap_mutex_lock(zchan->mutex);
		zap_buffer_zero(zchan->digit_buffer);
		zap_mutex_unlock(zchan->mutex);
	}
}

zap_status_t zap_channel_queue_dtmf(zap_channel_t *zchan, const char *dtmf)
{
	zap_status_t status;
	register zap_size_t len, inuse;
	zap_size_t wr = 0;
	const char *p;
	
	assert(zchan != NULL);

	zap_mutex_lock(zchan->mutex);

	inuse = zap_buffer_inuse(zchan->digit_buffer);
	len = strlen(dtmf);
	
	if (len + inuse > zap_buffer_len(zchan->digit_buffer)) {
		zap_buffer_toss(zchan->digit_buffer, strlen(dtmf));
	}

	if (zchan->span->dtmf_hangup_len) {
		for (p = dtmf; zap_is_dtmf(*p); p++) {
			memmove (zchan->dtmf_hangup_buf, zchan->dtmf_hangup_buf + 1, zchan->span->dtmf_hangup_len - 1);
			zchan->dtmf_hangup_buf[zchan->span->dtmf_hangup_len - 1] = *p;
			if (!strcmp(zchan->dtmf_hangup_buf, zchan->span->dtmf_hangup)) {
				zap_log(ZAP_LOG_DEBUG, "DTMF hangup detected.\n");
				zap_set_state_locked(zchan, ZAP_CHANNEL_STATE_HANGUP);
				break;
			}
		}
	}

	p = dtmf;
	while (wr < len && p) {
		if (zap_is_dtmf(*p)) {
			wr++;
		} else {
			break;
		}
		p++;
	}

	status = zap_buffer_write(zchan->digit_buffer, dtmf, wr) ? ZAP_SUCCESS : ZAP_FAIL;
	zap_mutex_unlock(zchan->mutex);
	
	return status;
}


static zap_status_t handle_dtmf(zap_channel_t *zchan, zap_size_t datalen)
{
	zap_buffer_t *buffer = NULL;
	zap_size_t dblen;

	if (zchan->gen_dtmf_buffer && (dblen = zap_buffer_inuse(zchan->gen_dtmf_buffer))) {
		char digits[128] = "";
		char *cur;
		int x = 0;				 
		
		if (dblen > sizeof(digits) - 1) {
			dblen = sizeof(digits) - 1;
		}

		if (zap_buffer_read(zchan->gen_dtmf_buffer, digits, dblen) && !zap_strlen_zero_buf(digits)) {
			zap_log(ZAP_LOG_DEBUG, "%d:%d GENERATE DTMF [%s]\n", zchan->span_id, zchan->chan_id, digits);	
		
			cur = digits;

			if (*cur == 'F') {
				zap_channel_command(zchan, ZAP_COMMAND_FLASH, NULL);
				cur++;
			}

			for (; *cur; cur++) {
				int wrote = 0;
				if ((wrote = teletone_mux_tones(&zchan->tone_session, &zchan->tone_session.TONES[(int)*cur]))) {
					zap_buffer_write(zchan->dtmf_buffer, zchan->tone_session.buffer, wrote * 2);
					x++;
				} else {
					zap_log(ZAP_LOG_ERROR, "%d:%d Problem Adding DTMF SEQ [%s]\n", zchan->span_id, zchan->chan_id, digits);
					return ZAP_FAIL;
				}
			}
		
			zchan->skip_read_frames = 200 * x;
		}
	}
	

	if (!zchan->buffer_delay || --zchan->buffer_delay == 0) {
		if (zchan->dtmf_buffer && (dblen = zap_buffer_inuse(zchan->dtmf_buffer))) {
			buffer = zchan->dtmf_buffer;
		} else if (zchan->fsk_buffer && (dblen = zap_buffer_inuse(zchan->fsk_buffer))) {
			buffer = zchan->fsk_buffer;			
		}
	}

	if (buffer) {
		zap_size_t dlen = datalen;
		uint8_t auxbuf[1024];
		zap_size_t len, br, max = sizeof(auxbuf);
		
		if (zchan->native_codec != ZAP_CODEC_SLIN) {
			dlen *= 2;
		}
		
		len = dblen > dlen ? dlen : dblen;

		br = zap_buffer_read(buffer, auxbuf, len);		
		if (br < dlen) {
			memset(auxbuf + br, 0, dlen - br);
		}

		if (zchan->native_codec != ZAP_CODEC_SLIN) {
			if (zchan->native_codec == ZAP_CODEC_ULAW) {
				zio_slin2ulaw(auxbuf, max, &dlen);
			} else if (zchan->native_codec == ZAP_CODEC_ALAW) {
				zio_slin2alaw(auxbuf, max, &dlen);
			}
		}
		
		return zchan->zio->write(zchan, auxbuf, &dlen);
	} 

	return ZAP_SUCCESS;

}



zap_status_t zap_channel_read(zap_channel_t *zchan, void *data, zap_size_t *datalen)
{
	zap_status_t status = ZAP_FAIL;
	zio_codec_t codec_func = NULL;
	zap_size_t max = *datalen;

	assert(zchan != NULL);
	assert(zchan->zio != NULL);
	assert(zchan->zio != NULL);
	
    if (!zap_test_flag(zchan, ZAP_CHANNEL_OPEN)) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "channel not open");
        return ZAP_FAIL;
    }

	if (!zchan->zio->read) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "method not implemented");
		return ZAP_FAIL;
	}

    status = zchan->zio->read(zchan, data, datalen);
	if (zchan->fds[0] > -1) {
		int dlen = (int) *datalen;
		if (write(zchan->fds[0], data, dlen) != dlen) {
			snprintf(zchan->last_error, sizeof(zchan->last_error), "file write error!");
			return ZAP_FAIL;
		}
	}

	if (status == ZAP_SUCCESS) {
		handle_dtmf(zchan, *datalen);
	}

	if (status == ZAP_SUCCESS && zap_test_flag(zchan, ZAP_CHANNEL_TRANSCODE) && zchan->effective_codec != zchan->native_codec) {
		if (zchan->native_codec == ZAP_CODEC_ULAW && zchan->effective_codec == ZAP_CODEC_SLIN) {
			codec_func = zio_ulaw2slin;
		} else if (zchan->native_codec == ZAP_CODEC_ULAW && zchan->effective_codec == ZAP_CODEC_ALAW) {
			codec_func = zio_ulaw2alaw;
		} else if (zchan->native_codec == ZAP_CODEC_ALAW && zchan->effective_codec == ZAP_CODEC_SLIN) {
			codec_func = zio_alaw2slin;
		} else if (zchan->native_codec == ZAP_CODEC_ALAW && zchan->effective_codec == ZAP_CODEC_ULAW) {
			codec_func = zio_alaw2ulaw;
		}

		if (codec_func) {
			status = codec_func(data, max, datalen);
		} else {
			snprintf(zchan->last_error, sizeof(zchan->last_error), "codec error!");
			status = ZAP_FAIL;
		}
	}

	if (zap_test_flag(zchan, ZAP_CHANNEL_DTMF_DETECT) || zap_test_flag(zchan, ZAP_CHANNEL_PROGRESS_DETECT) || 
		zap_test_flag(zchan, ZAP_CHANNEL_CALLERID_DETECT)) {
		uint8_t sln_buf[1024] = {0};
		int16_t *sln;
		zap_size_t slen = 0;
		char digit_str[80] = "";

		if (zchan->effective_codec == ZAP_CODEC_SLIN) {
			sln = data;
			slen = *datalen / 2;
		} else {
			zap_size_t len = *datalen;
			uint32_t i;
			uint8_t *lp = data;

			slen = sizeof(sln_buf) / 2;
			if (len > slen) {
				len = slen;
			}

			sln = (int16_t *) sln_buf;
			for(i = 0; i < len; i++) {
				if (zchan->effective_codec == ZAP_CODEC_ULAW) {
					*sln++ = ulaw_to_linear(*lp++);
				} else if (zchan->effective_codec == ZAP_CODEC_ALAW) {
					*sln++ = alaw_to_linear(*lp++);
				} else {
					snprintf(zchan->last_error, sizeof(zchan->last_error), "codec error!");
					return ZAP_FAIL;
				}
			}
			sln = (int16_t *) sln_buf;
			slen = len;
		}

		if (zap_test_flag(zchan, ZAP_CHANNEL_CALLERID_DETECT)) {
			if (zap_fsk_demod_feed(&zchan->fsk, sln, slen) != ZAP_SUCCESS) {
				zap_size_t type, mlen;
				char str[128], *sp;
				
				while(zap_fsk_data_parse(&zchan->fsk, &type, &sp, &mlen) == ZAP_SUCCESS) {
					*(str+mlen) = '\0';
					zap_copy_string(str, sp, ++mlen);
					zap_clean_string(str);
					zap_log(ZAP_LOG_DEBUG, "FSK: TYPE %s LEN %d VAL [%s]\n", zap_mdmf_type2str(type), mlen-1, str);
					
					switch(type) {
					case MDMF_DDN:
					case MDMF_PHONE_NUM:
						{
							if (mlen > sizeof(zchan->caller_data.ani)) {
								mlen = sizeof(zchan->caller_data.ani);
							}
							zap_set_string(zchan->caller_data.ani.digits, str);
							zap_set_string(zchan->caller_data.cid_num.digits, zchan->caller_data.ani.digits);
						}
						break;
					case MDMF_NO_NUM:
						{
							zap_set_string(zchan->caller_data.ani.digits, *str == 'P' ? "private" : "unknown");
							zap_set_string(zchan->caller_data.cid_name, zchan->caller_data.ani.digits);
						}
						break;
					case MDMF_PHONE_NAME:
						{
							if (mlen > sizeof(zchan->caller_data.cid_name)) {
								mlen = sizeof(zchan->caller_data.cid_name);
							}
							zap_set_string(zchan->caller_data.cid_name, str);
						}
						break;
					case MDMF_NO_NAME:
						{
							zap_set_string(zchan->caller_data.cid_name, *str == 'P' ? "private" : "unknown");
						}
					case MDMF_DATETIME:
						{
							if (mlen > sizeof(zchan->caller_data.cid_date)) {
								mlen = sizeof(zchan->caller_data.cid_date);
							}
							zap_set_string(zchan->caller_data.cid_date, str);
						}
						break;
					}
				}
				zap_channel_command(zchan, ZAP_COMMAND_DISABLE_CALLERID_DETECT, NULL);
			}
		}

		if (zap_test_flag(zchan, ZAP_CHANNEL_PROGRESS_DETECT) && !zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_PROGRESS)) {
			uint32_t i;

			for (i = 1; i < ZAP_TONEMAP_INVALID; i++) {
				if (zchan->span->tone_finder[i].tone_count) {
					if (zchan->needed_tones[i] && teletone_multi_tone_detect(&zchan->span->tone_finder[i], sln, (int)slen)) {
						if (++zchan->detected_tones[i]) {
							zchan->needed_tones[i] = 0;
							zchan->detected_tones[0]++;
						}
					}
				}
			}
		}

		if (zap_test_flag(zchan, ZAP_CHANNEL_DTMF_DETECT) && !zap_channel_test_feature(zchan, ZAP_CHANNEL_FEATURE_DTMF_DETECT)) {
			teletone_dtmf_detect(&zchan->dtmf_detect, sln, (int)slen);
			teletone_dtmf_get(&zchan->dtmf_detect, digit_str, sizeof(digit_str));

			if(*digit_str) {
				zio_event_cb_t event_callback = NULL;

				if (zchan->state == ZAP_CHANNEL_STATE_CALLWAITING && (*digit_str == 'D' || *digit_str == 'A')) {
					zchan->detected_tones[ZAP_TONEMAP_CALLWAITING_ACK]++;
				} else {
					zap_channel_queue_dtmf(zchan, digit_str);

					if (zchan->span->event_callback) {
						event_callback = zchan->span->event_callback;
					} else if (zchan->event_callback) {
						event_callback = zchan->event_callback;
					}

					if (event_callback) {
						zchan->event_header.channel = zchan;
						zchan->event_header.e_type = ZAP_EVENT_DTMF;
						zchan->event_header.data = digit_str;
						event_callback(zchan, &zchan->event_header);
						zchan->event_header.e_type = ZAP_EVENT_NONE;
						zchan->event_header.data = NULL;
					}
					if (zap_test_flag(zchan, ZAP_CHANNEL_SUPRESS_DTMF)) {
						zchan->skip_read_frames = 20;
					}
					if (zchan->skip_read_frames > 0) {
						memset(data, 0, *datalen);
						zchan->skip_read_frames--;
					}  
				}
			}
		}
	}

	return status;
}


zap_status_t zap_channel_write(zap_channel_t *zchan, void *data, zap_size_t datasize, zap_size_t *datalen)
{
	zap_status_t status = ZAP_FAIL;
	zio_codec_t codec_func = NULL;
	zap_size_t max = datasize;

	assert(zchan != NULL);
	assert(zchan->zio != NULL);

	if (!zchan->buffer_delay && 
		((zchan->dtmf_buffer && zap_buffer_inuse(zchan->dtmf_buffer)) ||
		 (zchan->fsk_buffer && zap_buffer_inuse(zchan->fsk_buffer)))) {
		/* read size writing DTMF ATM */
		return ZAP_SUCCESS;
	}


    if (!zap_test_flag(zchan, ZAP_CHANNEL_OPEN)) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "channel not open");
        return ZAP_FAIL;
    }

	if (!zchan->zio->write) {
		snprintf(zchan->last_error, sizeof(zchan->last_error), "method not implemented");
		return ZAP_FAIL;
	}
	
	if (zap_test_flag(zchan, ZAP_CHANNEL_TRANSCODE) && zchan->effective_codec != zchan->native_codec) {
		if (zchan->native_codec == ZAP_CODEC_ULAW && zchan->effective_codec == ZAP_CODEC_SLIN) {
			codec_func = zio_slin2ulaw;
		} else if (zchan->native_codec == ZAP_CODEC_ULAW && zchan->effective_codec == ZAP_CODEC_ALAW) {
			codec_func = zio_alaw2ulaw;
		} else if (zchan->native_codec == ZAP_CODEC_ALAW && zchan->effective_codec == ZAP_CODEC_SLIN) {
			codec_func = zio_slin2alaw;
		} else if (zchan->native_codec == ZAP_CODEC_ALAW && zchan->effective_codec == ZAP_CODEC_ULAW) {
			codec_func = zio_ulaw2alaw;
		}

		if (codec_func) {
			status = codec_func(data, max, datalen);
		} else {
			snprintf(zchan->last_error, sizeof(zchan->last_error), "codec error!");
			status = ZAP_FAIL;
		}
	}	
	
	if (zchan->fds[1] > -1) {
		int dlen = (int) *datalen;
		if ((write(zchan->fds[1], data, dlen)) != dlen) {
			snprintf(zchan->last_error, sizeof(zchan->last_error), "file write error!");
			return ZAP_FAIL;
		}
	}

    status = zchan->zio->write(zchan, data, datalen);

	return status;
}

zap_status_t zap_channel_clear_vars(zap_channel_t *zchan)
{
	if(zchan->variable_hash) {
		hashtable_destroy(zchan->variable_hash);
	}
	zchan->variable_hash = create_hashtable(16, zap_hash_hashfromstring, zap_hash_equalkeys);

	if(!zchan->variable_hash)
		return ZAP_FAIL;
	
	return ZAP_SUCCESS;
}

zap_status_t zap_channel_add_var(zap_channel_t *zchan, const char *var_name, const char *value)
{
	char *t_name = 0, *t_val = 0;

	if(!zchan->variable_hash || !var_name || !value)
	{
		return ZAP_FAIL;
	}

	t_name = strdup(var_name);
	t_val = strdup(value);

	if(hashtable_insert(zchan->variable_hash, t_name, t_val, HASHTABLE_FLAG_FREE_KEY | HASHTABLE_FLAG_FREE_VALUE)) {
		return ZAP_SUCCESS;
	}
	return ZAP_FAIL;
}

const char * zap_channel_get_var(zap_channel_t *zchan, const char *var_name)
{
	if(!zchan->variable_hash || !var_name)
	{
		return NULL;
	}
	return (const char *) hashtable_search(zchan->variable_hash, (void *)var_name);
}

static struct {
	zap_io_interface_t *pika_interface;
} interfaces;


char *zap_api_execute(const char *type, const char *cmd)
{
	zap_io_interface_t *zio = NULL;
	char *dup = NULL, *p;
	char *rval = NULL;

	if (type && !cmd) {
		dup = strdup(type);
		if ((p = strchr(dup, ' '))) {
			*p++ = '\0';
			cmd = p;
		}

		type = dup;
	}
	
	zap_mutex_lock(globals.mutex);
	if (!(zio = (zap_io_interface_t *) hashtable_search(globals.interface_hash, (void *)type))) {
		zap_load_module_assume(type);
		if ((zio = (zap_io_interface_t *) hashtable_search(globals.interface_hash, (void *)type))) {
			zap_log(ZAP_LOG_INFO, "auto-loaded '%s'\n", type);
		}
	}
	zap_mutex_unlock(globals.mutex);

	if (zio && zio->api) {
		zap_stream_handle_t stream = { 0 };
		zap_status_t status;
		ZAP_STANDARD_STREAM(stream);
		status = zio->api(&stream, cmd);
		
		if (status != ZAP_SUCCESS) {
			zap_safe_free(stream.data);
		} else {
			rval = (char *) stream.data;
		}
	}

	zap_safe_free(dup);
	
	return rval;
}


static zap_status_t load_config(void)
{
	char cfg_name[] = "openzap.conf";
	zap_config_t cfg;
	char *var, *val;
	int catno = -1;
	zap_span_t *span = NULL;
	unsigned configured = 0, d = 0;
	char name[80] = "";
	char number[25] = "";
	zap_io_interface_t *zio = NULL;
	zap_analog_start_type_t tmp;

	if (!zap_config_open_file(&cfg, cfg_name)) {
		return ZAP_FAIL;
	}
	
	while (zap_config_next_pair(&cfg, &var, &val)) {
		if (*cfg.category == '#') {
			if (cfg.catno != catno) {
				zap_log(ZAP_LOG_DEBUG, "Skipping %s\n", cfg.category);
				catno = cfg.catno;
			}
		} else if (!strncasecmp(cfg.category, "span", 4)) {
			if (cfg.catno != catno) {
				char *type = cfg.category + 4;
				char *name;
				
				if (*type == ' ') {
					type++;
				}
				
				zap_log(ZAP_LOG_DEBUG, "found config for span\n");
				catno = cfg.catno;
				
				if (zap_strlen_zero(type)) {
					zap_log(ZAP_LOG_CRIT, "failure creating span, no type specified.\n");
					span = NULL;
					continue;
				}

				if ((name = strchr(type, ' '))) {
					*name++ = '\0';
				}

				zap_mutex_lock(globals.mutex);
				if (!(zio = (zap_io_interface_t *) hashtable_search(globals.interface_hash, type))) {
					zap_load_module_assume(type);
					if ((zio = (zap_io_interface_t *) hashtable_search(globals.interface_hash, type))) {
						zap_log(ZAP_LOG_INFO, "auto-loaded '%s'\n", type);
					}
				}
				zap_mutex_unlock(globals.mutex);

				if (!zio) {
					zap_log(ZAP_LOG_CRIT, "failure creating span, no such type '%s'\n", type);
					span = NULL;
					continue;
				}

				if (!zio->configure_span) {
					zap_log(ZAP_LOG_CRIT, "failure creating span, no configure_span method for '%s'\n", type);
					span = NULL;
					continue;
				}

				if (zap_span_create(zio, &span) == ZAP_SUCCESS) {
					span->type = strdup(type);
					d = 0;

					zap_mutex_lock(globals.mutex);
					if (!zap_strlen_zero(name) && hashtable_search(globals.span_hash, (void *)name)) {
						zap_log(ZAP_LOG_WARNING, "name %s is already used, substituting 'span%d' as the name\n", name, span->span_id);
						name = NULL;
					}

					if (!name) {
						char buf[128] = "";
						snprintf(buf, sizeof(buf), "span%d", span->span_id);
						name = buf;
					}
					span->name = strdup(name);
					hashtable_insert(globals.span_hash, (void *)span->name, span, HASHTABLE_FLAG_NONE);
					zap_mutex_unlock(globals.mutex);

					zap_log(ZAP_LOG_DEBUG, "created span %d (%s) of type %s\n", span->span_id, span->name, type);
					
				} else {
					zap_log(ZAP_LOG_CRIT, "failure creating span of type %s\n", type);
					span = NULL;
					continue;
				}
			}

			if (!span) {
				continue;
			}

			zap_log(ZAP_LOG_DEBUG, "span %d [%s]=[%s]\n", span->span_id, var, val);
			
			if (!strcasecmp(var, "trunk_type")) {
				span->trunk_type = zap_str2zap_trunk_type(val);
				zap_log(ZAP_LOG_DEBUG, "setting trunk type to '%s'\n", zap_trunk_type2str(span->trunk_type)); 
			} else if (!strcasecmp(var, "name")) {
				if (!strcasecmp(val, "undef")) {
					*name = '\0';
				} else {
					zap_copy_string(name, val, sizeof(name));
				}
			} else if (!strcasecmp(var, "number")) {
				if (!strcasecmp(val, "undef")) {
					*number = '\0';
				} else {
					zap_copy_string(number, val, sizeof(number));
				}
			} else if (!strcasecmp(var, "analog-start-type")) {
				if (span->trunk_type == ZAP_TRUNK_FXS || span->trunk_type == ZAP_TRUNK_FXO || span->trunk_type == ZAP_TRUNK_EM) {
					if ((tmp = zap_str2zap_analog_start_type(val)) != ZAP_ANALOG_START_NA) {
						span->start_type = tmp;
						zap_log(ZAP_LOG_DEBUG, "changing start type to '%s'\n", zap_analog_start_type2str(span->start_type)); 
					}
				} else {
					zap_log(ZAP_LOG_ERROR, "This option is only valid on analog trunks!\n");
				}
			} else if (!strcasecmp(var, "fxo-channel")) {
				if (span->trunk_type == ZAP_TRUNK_NONE) {
					span->trunk_type = ZAP_TRUNK_FXO;										
					zap_log(ZAP_LOG_DEBUG, "setting trunk type to '%s' start(%s)\n", zap_trunk_type2str(span->trunk_type), 
							zap_analog_start_type2str(span->start_type));
				}
				if (span->trunk_type == ZAP_TRUNK_FXO) {
					configured += zio->configure_span(span, val, ZAP_CHAN_TYPE_FXO, name, number);
				} else {
					zap_log(ZAP_LOG_WARNING, "Cannot add FXO channels to an FXS trunk!\n");
				}
			} else if (!strcasecmp(var, "fxs-channel")) {
				if (span->trunk_type == ZAP_TRUNK_NONE) {
					span->trunk_type = ZAP_TRUNK_FXS;
					zap_log(ZAP_LOG_DEBUG, "setting trunk type to '%s' start(%s)\n", zap_trunk_type2str(span->trunk_type), 
							zap_analog_start_type2str(span->start_type));
				}
				if (span->trunk_type == ZAP_TRUNK_FXS) {
					configured += zio->configure_span(span, val, ZAP_CHAN_TYPE_FXS, name, number);
				} else {
					zap_log(ZAP_LOG_WARNING, "Cannot add FXS channels to an FXO trunk!\n");
				}
			} else if (!strcasecmp(var, "em-channel")) {
				if (span->trunk_type == ZAP_TRUNK_NONE) {
					span->trunk_type = ZAP_TRUNK_EM;
					zap_log(ZAP_LOG_DEBUG, "setting trunk type to '%s' start(%s)\n", zap_trunk_type2str(span->trunk_type), 
							zap_analog_start_type2str(span->start_type));
				}
				if (span->trunk_type == ZAP_TRUNK_EM) {
					configured += zio->configure_span(span, val, ZAP_CHAN_TYPE_EM, name, number);
				} else {
					zap_log(ZAP_LOG_WARNING, "Cannot add EM channels to a non-EM trunk!\n");
				}
			} else if (!strcasecmp(var, "b-channel")) {
				configured += zio->configure_span(span, val, ZAP_CHAN_TYPE_B, name, number);
			} else if (!strcasecmp(var, "d-channel")) {
				if (d) {
					zap_log(ZAP_LOG_WARNING, "ignoring extra d-channel\n");
				} else {
					zap_chan_type_t qtype;
					if (!strncasecmp(val, "lapd:", 5)) {
						qtype = ZAP_CHAN_TYPE_DQ931;
						val += 5;
					} else {
						qtype = ZAP_CHAN_TYPE_DQ921;
					}
					configured += zio->configure_span(span, val, qtype, name, number);
					d++;
				}
			} else if (!strcasecmp(var, "cas-channel")) {
				configured += zio->configure_span(span, val, ZAP_CHAN_TYPE_CAS, name, number);	
			} else if (!strcasecmp(var, "dtmf_hangup")) {
				span->dtmf_hangup = strdup(val);
				span->dtmf_hangup_len = strlen(val);
			} else {
				zap_log(ZAP_LOG_ERROR, "unknown span variable '%s'\n", var);
			}
		} else {
			zap_log(ZAP_LOG_ERROR, "unknown param [%s] '%s' / '%s'\n", cfg.category, var, val);
		}
	}
	zap_config_close_file(&cfg);

	zap_log(ZAP_LOG_INFO, "Configured %u channel(s)\n", configured);
	
	return configured ? ZAP_SUCCESS : ZAP_FAIL;
}

static zap_status_t process_module_config(zap_io_interface_t *zio)
{
	zap_config_t cfg;
	char *var, *val;
	char filename[256] = "";
	assert(zio != NULL);

	snprintf(filename, sizeof(filename), "%s.conf", zio->name);

	if (!zio->configure) {
		zap_log(ZAP_LOG_DEBUG, "Module %s does not support configuration.\n", zio->name);	
		return ZAP_FAIL;
	}

	if (!zap_config_open_file(&cfg, filename)) {
		zap_log(ZAP_LOG_ERROR, "Cannot open %s\n", filename);	
		return ZAP_FAIL;
	}

	while (zap_config_next_pair(&cfg, &var, &val)) {
		zio->configure(cfg.category, var, val, cfg.lineno);
	}

	zap_config_close_file(&cfg);	

	return ZAP_SUCCESS;
}

int zap_load_module(const char *name)
{
	zap_dso_lib_t lib;
	int count = 0, x = 0;
	char path[128] = "";
	char *err;
	zap_module_t *mod;

#ifdef WIN32
    const char *ext = ".dll";
    //const char *EXT = ".DLL";
#elif defined (MACOSX) || defined (DARWIN)
    const char *ext = ".dylib";
    //const char *EXT = ".DYLIB";
#else
    const char *ext = ".so";
    //const char *EXT = ".SO";
#endif
	
	if (*name == *ZAP_PATH_SEPARATOR) {
		snprintf(path, sizeof(path), "%s%s", name, ext);
	} else {
		snprintf(path, sizeof(path), "%s%s%s%s", ZAP_MOD_DIR, ZAP_PATH_SEPARATOR, name, ext);
	}
	
	if (!(lib = zap_dso_open(path, &err))) {
		zap_log(ZAP_LOG_ERROR, "Error loading %s [%s]\n", path, err);
		zap_safe_free(err);
		return 0;
	}
	
	if (!(mod = (zap_module_t *) zap_dso_func_sym(lib, "zap_module", &err))) {
		zap_log(ZAP_LOG_ERROR, "Error loading %s [%s]\n", path, err);
		zap_safe_free(err);
		return 0;
	}

	if (mod->io_load) {
		zap_io_interface_t *interface = NULL;

		if (mod->io_load(&interface) != ZAP_SUCCESS || !interface || !interface->name) {
			zap_log(ZAP_LOG_ERROR, "Error loading %s\n", path);
		} else {
			zap_log(ZAP_LOG_INFO, "Loading IO from %s [%s]\n", path, interface->name);
			zap_mutex_lock(globals.mutex);
			if (hashtable_search(globals.interface_hash, (void *)interface->name)) {
				zap_log(ZAP_LOG_ERROR, "Interface %s already loaded!\n", interface->name);
			} else {
				hashtable_insert(globals.interface_hash, (void *)interface->name, interface, HASHTABLE_FLAG_NONE);
				process_module_config(interface);
				x++;
			}
			zap_mutex_unlock(globals.mutex);
		}
	}

	if (mod->sig_load) {
		if (mod->sig_load() != ZAP_SUCCESS) {
			zap_log(ZAP_LOG_ERROR, "Error loading %s\n", path);
		} else {
			zap_log(ZAP_LOG_INFO, "Loading SIG from %s\n", path);
			x++;
		}
	}

	if (x) {
		char *p;
		mod->lib = lib;
		zap_set_string(mod->path, path);
		if (mod->name[0] == '\0') {
			if (!(p = strrchr(path, *ZAP_PATH_SEPARATOR))) {
				p = path;
			}
			zap_set_string(mod->name, p);
		}

		zap_mutex_lock(globals.mutex);
		if (hashtable_search(globals.module_hash, (void *)mod->name)) {
			zap_log(ZAP_LOG_ERROR, "Module %s already loaded!\n", mod->name);
			zap_dso_destroy(&lib);
		} else {
			hashtable_insert(globals.module_hash, (void *)mod->name, mod, HASHTABLE_FLAG_NONE);
			count++;
		}
		zap_mutex_unlock(globals.mutex);
	} else {
		zap_log(ZAP_LOG_ERROR, "Unloading %s\n", path);
		zap_dso_destroy(&lib);
	}
	
	return count;
}

int zap_load_module_assume(const char *name)
{
	char buf[256] = "";

	snprintf(buf, sizeof(buf), "ozmod_%s", name);
	return zap_load_module(buf);
}

int zap_load_modules(void)
{
	char cfg_name[] = "modules.conf";
	zap_config_t cfg;
	char *var, *val;
	int count = 0;

	if (!zap_config_open_file(&cfg, cfg_name)) {
        return ZAP_FAIL;
    }

	while (zap_config_next_pair(&cfg, &var, &val)) {
        if (!strcasecmp(cfg.category, "modules")) {
			if (!strcasecmp(var, "load")) {
				count += zap_load_module(val);
			}
		}
	}
			
	return count;
}

zap_status_t zap_unload_modules(void)
{
	zap_hash_iterator_t *i;
	zap_dso_lib_t lib;

	for (i = hashtable_first(globals.module_hash); i; i = hashtable_next(i)) {
		const void *key;
		void *val;

		hashtable_this(i, &key, NULL, &val);
		
		if (key && val) {
			zap_module_t *mod = (zap_module_t *) val;

			if (!mod) {
				continue;
			}

			if (mod->io_unload) {
				if (mod->io_unload() == ZAP_SUCCESS) {
					zap_log(ZAP_LOG_INFO, "Unloading IO %s\n", mod->name);
				} else {
					zap_log(ZAP_LOG_ERROR, "Error unloading IO %s\n", mod->name);
				}
			} 

			if (mod->sig_unload) {
				if (mod->sig_unload() == ZAP_SUCCESS) {
					zap_log(ZAP_LOG_INFO, "Unloading SIG %s\n", mod->name);
				} else {
					zap_log(ZAP_LOG_ERROR, "Error unloading SIG %s\n", mod->name);
				}
			} 
			

			zap_log(ZAP_LOG_INFO, "Unloading %s\n", mod->path);
			lib = mod->lib;
			zap_dso_destroy(&lib);
			
		}
	}

	return ZAP_SUCCESS;
}

zap_status_t zap_configure_span(const char *type, zap_span_t *span, zio_signal_cb_t sig_cb, ...)
{
	zap_module_t *mod = (zap_module_t *) hashtable_search(globals.module_hash, (void *)type);
	zap_status_t status = ZAP_FAIL;

	if (!mod) {
		zap_load_module_assume(type);
		if ((mod = (zap_module_t *) hashtable_search(globals.module_hash, (void *)type))) {
			zap_log(ZAP_LOG_INFO, "auto-loaded '%s'\n", type);
		}
	}

	if (mod && mod->sig_configure) {
		va_list ap;
		va_start(ap, sig_cb);
		status = mod->sig_configure(span, sig_cb, ap);
		va_end(ap);
	} else {
		zap_log(ZAP_LOG_ERROR, "can't find '%s'\n", type);
		status = ZAP_FAIL;
	}

	return status;
}

zap_status_t zap_span_start(zap_span_t *span)
{
	if (span->start) {
		return span->start(span);
	}

	return ZAP_FAIL;
}


zap_status_t zap_global_init(void)
{
	int modcount;

	memset(&globals, 0, sizeof(globals));

	time_init();
	
	memset(&interfaces, 0, sizeof(interfaces));
	globals.interface_hash = create_hashtable(16, zap_hash_hashfromstring, zap_hash_equalkeys);
	globals.module_hash = create_hashtable(16, zap_hash_hashfromstring, zap_hash_equalkeys);
	globals.span_hash = create_hashtable(16, zap_hash_hashfromstring, zap_hash_equalkeys);
	modcount = 0;
	zap_mutex_create(&globals.mutex);
	
	modcount = zap_load_modules();
	zap_log(ZAP_LOG_NOTICE, "Modules configured: %d \n", modcount);

	if (load_config() == ZAP_SUCCESS) {
		globals.running = 1;
		return ZAP_SUCCESS;
	}

	zap_log(ZAP_LOG_ERROR, "No modules configured!\n");
	return ZAP_FAIL;
}

uint32_t zap_running(void)
{
	return globals.running;
}


zap_status_t zap_global_destroy(void)
{
	unsigned int i,j;
	time_end();

	globals.running = 0;	
	zap_span_close_all();
	zap_sleep(1000);

	for(i = 1; i <= globals.span_index; i++) {
		zap_span_t *cur_span = globals.spans[i];

		if (cur_span) {
			if (zap_test_flag(cur_span, ZAP_SPAN_CONFIGURED)) {
				zap_mutex_lock(cur_span->mutex);
				zap_clear_flag(cur_span, ZAP_SPAN_CONFIGURED);
				for(j = 1; j <= cur_span->chan_count && cur_span->channels[j]; j++) {
					zap_channel_t *cur_chan = cur_span->channels[j];
					if (cur_chan) {
						if (zap_test_flag(cur_chan, ZAP_CHANNEL_CONFIGURED)) {
							zap_channel_destroy(cur_chan);
						}
						free(cur_chan);
						cur_chan = NULL;
					}
				}
				zap_mutex_unlock(cur_span->mutex);

				if (cur_span->mutex) {
					zap_mutex_destroy(&cur_span->mutex);
				}

				zap_safe_free(cur_span->signal_data);
				zap_span_destroy(cur_span);
			}
			zap_mutex_lock(globals.mutex);
			hashtable_remove(globals.span_hash, (void *)cur_span->name);
			zap_mutex_unlock(globals.mutex);
			zap_safe_free(cur_span->type);
			zap_safe_free(cur_span->name);
			free(cur_span);
			cur_span = NULL;
		}
	}
	globals.span_index = 0;
	
	zap_unload_modules();

	zap_mutex_lock(globals.mutex);
	hashtable_destroy(globals.interface_hash);
	hashtable_destroy(globals.module_hash);
	hashtable_destroy(globals.span_hash);
	zap_mutex_unlock(globals.mutex);
	zap_mutex_destroy(&globals.mutex);

	memset(&globals, 0, sizeof(globals));
	return ZAP_SUCCESS;
}


uint32_t zap_separate_string(char *buf, char delim, char **array, int arraylen)
{
	int argc;
	char *ptr;
	int quot = 0;
	char qc = '\'';
	int x;

	if (!buf || !array || !arraylen) {
		return 0;
	}

	memset(array, 0, arraylen * sizeof(*array));

	ptr = buf;

	for (argc = 0; *ptr && (argc < arraylen - 1); argc++) {
		array[argc] = ptr;
		for (; *ptr; ptr++) {
			if (*ptr == qc) {
				if (quot) {
					quot--;
				} else {
					quot++;
				}
			} else if ((*ptr == delim) && !quot) {
				*ptr++ = '\0';
				break;
			}
		}
	}

	if (*ptr) {
		array[argc++] = ptr;
	}

	/* strip quotes and leading / trailing spaces */
	for (x = 0; x < argc; x++) {
		char *p;

		while(*(array[x]) == ' ') {
			(array[x])++;
		}
		p = array[x];
		while((p = strchr(array[x], qc))) {
			memmove(p, p+1, strlen(p));
			p++;
		}
		p = array[x] + (strlen(array[x]) - 1);
		while(*p == ' ') {
			*p-- = '\0';
		}
	}

	return argc;
}

void zap_bitstream_init(zap_bitstream_t *bsp, uint8_t *data, uint32_t datalen, zap_endian_t endian, uint8_t ss)
{
	memset(bsp, 0, sizeof(*bsp));
	bsp->data = data;
	bsp->datalen = datalen;
	bsp->endian = endian;
	bsp->ss = ss;
	
	if (endian < 0) {
		bsp->top = bsp->bit_index = 7;
		bsp->bot = 0;
	} else {
		bsp->top = bsp->bit_index = 0;
		bsp->bot = 7;
	}

}

int8_t zap_bitstream_get_bit(zap_bitstream_t *bsp)
{
	int8_t bit = -1;
	

	if (bsp->byte_index >= bsp->datalen) {
		goto done;
	}

	if (bsp->ss) {
		if (!bsp->ssv) {
			bsp->ssv = 1;
			return 0;
		} else if (bsp->ssv == 2) {
			bsp->byte_index++;
			bsp->ssv = 0;
			return 1;
		}
	}




	bit = (bsp->data[bsp->byte_index] >> (bsp->bit_index)) & 1;
	
	if (bsp->bit_index == bsp->bot) {
		bsp->bit_index = bsp->top;
		if (bsp->ss) {
			bsp->ssv = 2;
			goto done;
		} 

		if (++bsp->byte_index > bsp->datalen) {
			bit = -1;
			goto done;
		}
		
	} else {
		bsp->bit_index = bsp->bit_index + bsp->endian;
	}


 done:
	return bit;
}

void print_hex_bytes(uint8_t *data, zap_size_t dlen, char *buf, zap_size_t blen)
{
	char *bp = buf;
	uint8_t *byte = data;
	uint32_t i, j = 0;

	if (blen < (dlen * 3) + 2) {
        return;
    }

	*bp++ = '[';
	j++;

	for(i = 0; i < dlen; i++) {
		snprintf(bp, blen-j, "%02x ", *byte++);
		bp += 3;
		j += 3;
	}

	*--bp = ']';

}

void print_bits(uint8_t *b, int bl, char *buf, int blen, zap_endian_t e, uint8_t ss)
{
	zap_bitstream_t bs;
	int j = 0, c = 0;
	int8_t bit;
	uint32_t last;

	if (blen < (bl * 10) + 2) {
        return;
    }

	zap_bitstream_init(&bs, b, bl, e, ss);
	last = bs.byte_index;	
	while((bit = zap_bitstream_get_bit(&bs)) > -1) {
		buf[j++] = bit ? '1' : '0';
		if (bs.byte_index != last) {
			buf[j++] = ' ';
			last = bs.byte_index;
			if (++c == 8) {
				buf[j++] = '\n';
				c = 0;
			}
		}
	}

}



zap_status_t zap_console_stream_raw_write(zap_stream_handle_t *handle, uint8_t *data, zap_size_t datalen)
{
	zap_size_t need = handle->data_len + datalen;
	
	if (need >= handle->data_size) {
		void *new_data;
		need += handle->alloc_chunk;

		if (!(new_data = realloc(handle->data, need))) {
			return ZAP_MEMERR;
		}

		handle->data = new_data;
		handle->data_size = need;
	}

	memcpy((uint8_t *) (handle->data) + handle->data_len, data, datalen);
	handle->data_len += datalen;
	handle->end = (uint8_t *) (handle->data) + handle->data_len;
	*(uint8_t *)handle->end = '\0';

	return ZAP_SUCCESS;
}

zap_status_t zap_console_stream_write(zap_stream_handle_t *handle, const char *fmt, ...)
{
	va_list ap;
	char *buf = handle->data;
	char *end = handle->end;
	int ret = 0;
	char *data = NULL;

	if (handle->data_len >= handle->data_size) {
		return ZAP_FAIL;
	}

	va_start(ap, fmt);
	ret = vasprintf(&data, fmt, ap);
	va_end(ap);

	if (data) {
		zap_size_t remaining = handle->data_size - handle->data_len;
		zap_size_t need = strlen(data) + 1;

		if ((remaining < need) && handle->alloc_len) {
			zap_size_t new_len;
			void *new_data;

			new_len = handle->data_size + need + handle->alloc_chunk;
			if ((new_data = realloc(handle->data, new_len))) {
				handle->data_size = handle->alloc_len = new_len;
				handle->data = new_data;
				buf = handle->data;
				remaining = handle->data_size - handle->data_len;
				handle->end = (uint8_t *) (handle->data) + handle->data_len;
				end = handle->end;
			} else {
				zap_log(ZAP_LOG_CRIT, "Memory Error!\n");
				free(data);
				return ZAP_FAIL;
			}
		}

		if (remaining < need) {
			ret = -1;
		} else {
			ret = 0;
			snprintf(end, remaining, "%s", data);
			handle->data_len = strlen(buf);
			handle->end = (uint8_t *) (handle->data) + handle->data_len;
		}
		free(data);
	}

	return ret ? ZAP_FAIL : ZAP_SUCCESS;
}



/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */
