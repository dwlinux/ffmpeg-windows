/*
 * FILE:    video_display/quicktime.h
 * AUTHORS: Martin Benes     <martinbenesh@gmail.com>
 *          Lukas Hejtmanek  <xhejtman@ics.muni.cz>
 *          Petr Holub       <hopet@ics.muni.cz>
 *          Milos Liska      <xliska@fi.muni.cz>
 *          Jiri Matela      <matela@ics.muni.cz>
 *          Dalibor Matura   <255899@mail.muni.cz>
 *          Ian Wesley-Smith <iwsmith@cct.lsu.edu>
 *
 * Copyright (c) 2005-2209 CESNET z.s.p.o.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 
 *      This product includes software developed by CESNET z.s.p.o.
 * 
 * 4. Neither the name of CESNET nor the names of its contributors may be used 
 *    to endorse or promote products derived from this software without specific
 *    prior written permission.
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
 *
 *
 */

#define DISPLAY_QUICKTIME_ID    0xba370f2f
#include "video.h"

        struct audio_frame;

        typedef struct {
        char   *device;
        char   *input;
        unsigned int    width;
        unsigned int    height;
        double          fps;
        int             aux;
} quicktime_mode_t;

extern const quicktime_mode_t quicktime_modes[];

display_type_t          *display_quicktime_probe(void);
void                    *display_quicktime_init(char *fmt, unsigned int flags);
void                     display_quicktime_run(void *state);
void                     display_quicktime_finish(void *state);
void                     display_quicktime_done(void *state);
struct video_frame      *display_quicktime_getf(void *state);
int                      display_quicktime_putf(void *state, char *frame);
int                      display_quicktime_reconfigure(void *state, struct video_desc desc);
int                      display_quicktime_get_property(void *state, int property, void *val, size_t *len);

struct audio_frame      *display_quicktime_get_audio_frame(void *state);
void                     display_quicktime_put_audio_frame(void *state, struct audio_frame *frame);
int                      display_quicktime_reconfigure_audio(void *state, int quant_samples, int channels,
                int sample_rate);

