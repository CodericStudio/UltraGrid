/**
 * @file   video_capture/muxer.c
 * @author Gerard Castillo <gerard.castillo@i2cat.net>
 *
 * @brief Muxer of video capturers, N to 1.
 */
/*
 * Copyright (c) 2005-2010 Fundació i2CAT, Internet I Innovació Digital a Catalunya
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Fundació i2CAT nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "host.h"
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"

#include "debug.h"
#include "video.h"
#include "video_capture.h"

#include "tv.h"

#include "video_capture/muxer.h"
#include "audio/audio.h"

#include <stdio.h>
#include <stdlib.h>

/* prototypes of functions defined in this module */
static void show_help(void);

static void show_help()
{
        printf("Muxer capture\n");
        printf("Usage\n");
        printf("\t-t muxer -t <dev1_config> -t <dev2_config> ....]\n");
        printf("\t\twhere devn_config is a complete configuration string of device involved in the muxer device\n");

}

struct vidcap_muxer_state {
        struct vidcap     **devices;
        int                 devices_cnt;

        struct video_frame       *frame; 
        int frames;
        struct       timeval t, t0;

        int          audio_source_index;
};


struct vidcap_type *
vidcap_muxer_probe(void)
{
	struct vidcap_type*		vt;
    
	vt = (struct vidcap_type *) malloc(sizeof(struct vidcap_type));
	if (vt != NULL) {
		vt->id          = VIDCAP_MUXER_ID;
		vt->name        = "muxer";
		vt->description = "Muxer video capture";
	}
	return vt;
}

void *
vidcap_muxer_init(const struct vidcap_params *params)
{
	struct vidcap_muxer_state *s;
        int i;

	printf("vidcap_muxer_init\n");


        s = (struct vidcap_muxer_state *) calloc(1, sizeof(struct vidcap_muxer_state));
	if(s == NULL) {
		printf("Unable to allocate muxer capture state\n");
		return NULL;
	}

        s->audio_source_index = -1;
        s->frames = 0;
        gettimeofday(&s->t0, NULL);

        if(vidcap_params_get_fmt(params) && strcmp(vidcap_params_get_fmt(params), "") != 0) {
                show_help();
                return &vidcap_init_noerr;
        }


        s->devices_cnt = 0;
        const struct vidcap_params *tmp = params;
        while((tmp = vidcap_params_get_next(tmp))) {
                if (vidcap_params_get_driver(tmp) != NULL)
                        s->devices_cnt++;
                else
                        break;
        }

        s->devices = calloc(s->devices_cnt, sizeof(struct vidcap *));
        i = 0;
        tmp = params;
        for (int i = 0; i < s->devices_cnt; ++i) {
                tmp = vidcap_params_get_next(tmp);

                int ret = initialize_video_capture(NULL, tmp, &s->devices[i]);
                if(ret != 0) {
                        fprintf(stderr, "[muxer] Unable to initialize device %d (%s:%s).\n",
                                        i, vidcap_params_get_driver(tmp),
                                        vidcap_params_get_fmt(tmp));
                        goto error;
                }
        }

        s->frame = vf_alloc(s->devices_cnt);
        
	return s;

error:
        if(s->devices) {
                int i;
                for (i = 0u; i < s->devices_cnt; ++i) {
                        if(s->devices[i]) {
                                 vidcap_done(s->devices[i]);
                        }
                }
        }
        free(s);
        return NULL;
}

void
vidcap_muxer_done(void *state)
{
	struct vidcap_muxer_state *s = (struct vidcap_muxer_state *) state;

	assert(s != NULL);

	if (s != NULL) {
                int i;
		for (i = 0; i < s->devices_cnt; ++i) {
                         vidcap_done(s->devices[i]);
		}
	}
        
        vf_free(s->frame);
}

struct video_frame *
vidcap_muxer_grab(void *state, struct audio_frame **audio)
{
	struct vidcap_muxer_state *s = (struct vidcap_muxer_state *) state;
        struct audio_frame *audio_frame = NULL;
        struct video_frame *frame = NULL;

        if(audio_frame) {
                *audio = audio_frame;
        } else {
                *audio = NULL;
        }
        for (int i = 0; i < s->devices_cnt; ++i) {
                frame = NULL;
                while(!frame) {
                        frame = vidcap_grab(s->devices[i], &audio_frame);
                }
                if (i == 0) {
                        s->frame->color_spec = frame->color_spec;
                        s->frame->interlacing = frame->interlacing;
                        s->frame->fps = frame->fps;
                }
                if (s->audio_source_index == -1 && audio_frame != NULL) {
                        fprintf(stderr, "[muxer] Locking device #%d as an audio source.\n",
                                        i);
                        s->audio_source_index = i;
                }
                if (s->audio_source_index == i) {
                        *audio = audio_frame;
                }
                if (frame->color_spec != s->frame->color_spec ||
                                frame->fps != s->frame->fps ||
                                frame->interlacing != s->frame->interlacing) {
                        fprintf(stderr, "[muxer] Different format detected: ");
                        if(frame->color_spec != s->frame->color_spec)
                                fprintf(stderr, "codec");
                        if(frame->interlacing != s->frame->interlacing)
                                fprintf(stderr, "interlacing");
                        if(frame->fps != s->frame->fps)
                                fprintf(stderr, "FPS (%.2f and %.2f)", frame->fps, s->frame->fps);
                        fprintf(stderr, "\n");
                        
                        return NULL;
                }
                vf_get_tile(s->frame, i)->width = vf_get_tile(frame, 0)->width;
                vf_get_tile(s->frame, i)->height = vf_get_tile(frame, 0)->height;
                vf_get_tile(s->frame, i)->data_len = vf_get_tile(frame, 0)->data_len;
                vf_get_tile(s->frame, i)->data = vf_get_tile(frame, 0)->data;
        }
        s->frames++;
        gettimeofday(&s->t, NULL);
        double seconds = tv_diff(s->t, s->t0);    
        if (seconds >= 5) {
            float fps  = s->frames / seconds;
            fprintf(stderr, "[muxer cap.] %d frames in %g seconds = %g FPS\n", s->frames, seconds, fps);
            s->t0 = s->t;
            s->frames = 0;
        }  

	return s->frame;
}

