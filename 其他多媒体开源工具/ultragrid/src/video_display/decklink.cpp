/*
 * FILE:    video_display/decklink.cpp
 * AUTHORS: Martin Benes     <martinbenesh@gmail.com>
 *          Lukas Hejtmanek  <xhejtman@ics.muni.cz>
 *          Petr Holub       <hopet@ics.muni.cz>
 *          Milos Liska      <xliska@fi.muni.cz>
 *          Jiri Matela      <matela@ics.muni.cz>
 *          Dalibor Matura   <255899@mail.muni.cz>
 *          Ian Wesley-Smith <iwsmith@cct.lsu.edu>
 *          Colin Perkins    <csp@isi.edu>
 *
 * Copyright (c) 2005-2010 CESNET z.s.p.o.
 * Copyright (c) 2001-2003 University of Southern California
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
 *      This product includes software developed by the University of Southern
 *      California Information Sciences Institute. This product also includes
 *      software developed by CESNET z.s.p.o.
 * 
 * 4. Neither the name of the University, Institute, CESNET nor the names of
 *    its contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
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
#define MODULE_NAME "[Decklink display] "

#ifdef __cplusplus
extern "C" {
#endif

#include "host.h"
#include "debug.h"
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#include "video_codec.h"
#include "tv.h"
#include "video_display/decklink.h"
#include "debug.h"
#include "video_capture.h"
#include "audio/audio.h"
#include "audio/utils.h"

#include "DeckLinkAPI.h"
#include "DeckLinkAPIVersion.h"

#ifdef __cplusplus
} // END of extern "C"
#endif

#ifdef HAVE_MACOSX
#define STRING CFStringRef
#else
#define STRING const char *
#endif

enum link {
        LINK_UNSPECIFIED,
        LINK_3G,
        LINK_DUAL
};

// defined int video_capture/decklink.cpp
void print_output_modes(IDeckLink *);
static int blackmagic_api_version_check(STRING *current_version);


#define MAX_DEVICES 4

class PlaybackDelegate : public IDeckLinkVideoOutputCallback // , public IDeckLinkAudioOutputCallback
{
        struct state_decklink *                 s;
        int                                     i;

public:
        PlaybackDelegate (struct state_decklink* owner, int index);

        // IUnknown needs only a dummy implementation
        virtual HRESULT         QueryInterface (REFIID iid, LPVOID *ppv)        {return E_NOINTERFACE;}
        virtual ULONG           AddRef ()                                                                       {return 1;}
        virtual ULONG           Release ()                                                                      {return 1;}

        virtual HRESULT         ScheduledFrameCompleted (IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);
        virtual HRESULT         ScheduledPlaybackHasStopped ();
        //virtual HRESULT         RenderAudioSamples (bool preroll);
};


class DeckLinkTimecode : public IDeckLinkTimecode{
                BMDTimecodeBCD timecode;
        public:
                DeckLinkTimecode() : timecode(0) {}
                /* IDeckLinkTimecode */
                virtual BMDTimecodeBCD GetBCD (void) { return timecode; }
                virtual HRESULT GetComponents (/* out */ uint8_t *hours, /* out */ uint8_t *minutes, /* out */ uint8_t *seconds, /* out */ uint8_t *frames) { 
                        *frames =   (timecode & 0xf)              + ((timecode & 0xf0) >> 4) * 10;
                        *seconds = ((timecode & 0xf00) >> 8)      + ((timecode & 0xf000) >> 12) * 10;
                        *minutes = ((timecode & 0xf0000) >> 16)   + ((timecode & 0xf00000) >> 20) * 10;
                        *hours =   ((timecode & 0xf000000) >> 24) + ((timecode & 0xf0000000) >> 28) * 10;
                        return S_OK;
                }
                virtual HRESULT GetString (/* out */ STRING *timecode) { return E_FAIL; }
                virtual BMDTimecodeFlags GetFlags (void)        { return bmdTimecodeFlagDefault; }
                virtual HRESULT GetTimecodeUserBits (/* out */ BMDTimecodeUserBits *userBits) { if (!userBits) return E_POINTER; else return S_OK; }

                /* IUnknown */
                virtual HRESULT         QueryInterface (REFIID iid, LPVOID *ppv)        {return E_NOINTERFACE;}
                virtual ULONG           AddRef ()                                                                       {return 1;}
                virtual ULONG           Release ()                                                                      {return 1;}
                
                void SetBCD(BMDTimecodeBCD timecode) { this->timecode = timecode; }
        };

class DeckLinkFrame;
class DeckLinkFrame : public IDeckLinkMutableVideoFrame
{
                long ref;
                
                long width;
                long height;
                long rawBytes;
                BMDPixelFormat pixelFormat;
                char *data;

                IDeckLinkTimecode *timecode;

        protected:
                DeckLinkFrame(long w, long h, long rb, BMDPixelFormat pf); 
                virtual ~DeckLinkFrame();

        public:
        	static DeckLinkFrame *Create(long width, long height, long rawBytes, BMDPixelFormat pixelFormat);                
                /* IUnknown */
                virtual HRESULT QueryInterface(REFIID, void**);
                virtual ULONG AddRef();
                virtual ULONG Release();
                
                /* IDeckLinkVideoFrame */
                long GetWidth (void);
                long GetHeight (void);
                long GetRowBytes (void);
                BMDPixelFormat GetPixelFormat (void);
                BMDFrameFlags GetFlags (void);
                HRESULT GetBytes (/* out */ void **buffer);
                
                HRESULT GetTimecode (/* in */ BMDTimecodeFormat format, /* out */ IDeckLinkTimecode **timecode);
                HRESULT GetAncillaryData (/* out */ IDeckLinkVideoFrameAncillary **ancillary);
                

                /* IDeckLinkMutableVideoFrame */
                HRESULT SetFlags(BMDFrameFlags);
                HRESULT SetTimecode(BMDTimecodeFormat, IDeckLinkTimecode*);
                HRESULT SetTimecodeFromComponents(BMDTimecodeFormat, uint8_t, uint8_t, uint8_t, uint8_t, BMDTimecodeFlags);
                HRESULT SetAncillaryData(IDeckLinkVideoFrameAncillary*);
                HRESULT SetTimecodeUserBits(BMDTimecodeFormat, BMDTimecodeUserBits);
};

class DeckLink3DFrame : public DeckLinkFrame, public IDeckLinkVideoFrame3DExtensions
{
        private:
                DeckLink3DFrame(long w, long h, long rb, BMDPixelFormat pf); 
                ~DeckLink3DFrame();
                
                long ref;
                
                long width;
                long height;
                long rawBytes;
                BMDPixelFormat pixelFormat;
                DeckLinkFrame *rightEye;

        public:
                static DeckLink3DFrame *Create(long width, long height, long rawBytes, BMDPixelFormat pixelFormat);
                
                /* IUnknown */
                HRESULT QueryInterface(REFIID, void**);
                ULONG AddRef();
                ULONG Release();

                /* IDeckLinkVideoFrame3DExtensions */
                BMDVideo3DPackingFormat Get3DPackingFormat();
                HRESULT GetFrameForRightEye(IDeckLinkVideoFrame**);
};

#define DECKLINK_MAGIC DISPLAY_DECKLINK_ID

struct device_state {
        PlaybackDelegate        *delegate;
        IDeckLink               *deckLink;
        IDeckLinkOutput         *deckLinkOutput;
        IDeckLinkMutableVideoFrame *deckLinkFrame;
        IDeckLinkConfiguration*         deckLinkConfiguration;
};

struct state_decklink {
        uint32_t            magic;

        struct timeval      tv;

        struct device_state state[MAX_DEVICES];

        BMDTimeValue        frameRateDuration;
        BMDTimeScale        frameRateScale;

        DeckLinkTimecode    *timecode;

        struct audio_frame  audio;
        struct video_frame *frame;

        unsigned long int   frames;
        unsigned long int   frames_last;
        bool                stereo;
        bool                initialized;
        bool                emit_timecode;
        int                 devices_cnt;
        unsigned int        play_audio:1;

        int                 output_audio_channel_count;
        BMDPixelFormat      pixelFormat;

        enum link           link;

        /*
         * Fast mode means partially incorrect moder when there are data
         * continually written to same video frame instaed to separate ones.
         */
        bool                fast;
 };

static void show_help(void);

static void show_help(void)
{
        IDeckLinkIterator*              deckLinkIterator;
        IDeckLink*                      deckLink;
        int                             numDevices = 0;
        HRESULT                         result;

        printf("Decklink (output) options:\n");
        printf("\t-d decklink:<device_number(s)>[:timecode][:3G|:dual-link][:3D[:HDMI3DPacking=<packing>]][:fast]\n");
        printf("\t\tcoma-separated numbers of output devices\n");
        // Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
        deckLinkIterator = CreateDeckLinkIteratorInstance();
        if (deckLinkIterator == NULL)
        {
		fprintf(stderr, "\nA DeckLink iterator could not be created. The DeckLink drivers may not be installed or are outdated.\n");
		fprintf(stderr, "This UltraGrid version was compiled with DeckLink drivers %s. You should have at least this version.\n\n",
                                BLACKMAGIC_DECKLINK_API_VERSION_STRING);
                return;
        }
        
        // Enumerate all cards in this system
        while (deckLinkIterator->Next(&deckLink) == S_OK)
        {
                STRING          deviceNameString = NULL;
                const char *deviceNameCString;
                
                // *** Print the model name of the DeckLink card
                result = deckLink->GetModelName((STRING *) &deviceNameString);
#ifdef HAVE_MACOSX
                deviceNameCString = (char *) malloc(128);
                CFStringGetCString(deviceNameString, (char *) deviceNameCString, 128, kCFStringEncodingMacRoman);
#else
                deviceNameCString = deviceNameString;
#endif
                if (result == S_OK)
                {
                        printf("\ndevice: %d.) %s \n\n",numDevices, deviceNameCString);
                        print_output_modes(deckLink);
#ifdef HAVE_MACOSX
                        CFRelease(deviceNameString);
#endif
                        free((void *)deviceNameCString);
                }
                
                // Increment the total number of DeckLink cards found
                numDevices++;
        
                // Release the IDeckLink instance when we've finished with it to prevent leaks
                deckLink->Release();
        }
        
        deckLinkIterator->Release();

        // If no DeckLink cards were found in the system, inform the user
        if (numDevices == 0)
        {
                printf("\nNo Blackmagic Design devices were found.\n");
                return;
        } 

        printf("\nHDMI 3D packing can be one of following (optional for HDMI 1.4, mandatory for pre-1.4 HDMI):\n");
        printf("\tSideBySideHalf\n");
        printf("\tLineByLine\n");
        printf("\tTopAndBottom\n");
        printf("\tFramePacking\n");
        printf("\tLeftOnly\n");
        printf("\tRightOnly\n");
        printf("\n");

        printf("Fast mode has lower latency at the expense of incorrect displaying (frame rewriting).\n");
        printf("\n");
}


struct video_frame *
display_decklink_getf(void *state)
{
        struct state_decklink *s = (struct state_decklink *)state;

        assert(s->magic == DECKLINK_MAGIC);

        if (s->initialized) {
                if(s->stereo) {
                        IDeckLinkVideoFrame     *deckLinkFrameRight;
                        assert(s->devices_cnt == 1);
                        if(!s->fast) {
                                s->state[0].deckLinkFrame = DeckLink3DFrame::Create(s->frame->tiles[0].width, s->frame->tiles[0].height,
                                                s->frame->tiles[0].linesize, s->pixelFormat);
                        }
                                
                        s->state[0].deckLinkFrame->GetBytes((void **) &s->frame->tiles[0].data);
                        
                        dynamic_cast<DeckLink3DFrame *>(s->state[0].deckLinkFrame)->GetFrameForRightEye(&deckLinkFrameRight);
                        deckLinkFrameRight->GetBytes((void **) &s->frame->tiles[1].data);
                        // release immedieatelly (parent still holds the reference)
                        deckLinkFrameRight->Release();
                } else {
                        for(int i = 0; i < s->devices_cnt; ++i) {
                                if(!s->fast) {
                                        s->state[i].deckLinkFrame = DeckLinkFrame::Create(s->frame->tiles[0].width, s->frame->tiles[0].height,
                                                        s->frame->tiles[0].linesize, s->pixelFormat);
                                }
                                
                                s->state[i].deckLinkFrame->GetBytes((void **) &s->frame->tiles[i].data);
                        }
                }
        }

        /* stub -- real frames are taken with get_sub_frame call */
        return s->frame;
}

static void update_timecode(DeckLinkTimecode *tc, double fps)
{
        const float epsilon = 0.005;
        int shifted;
        uint8_t hours, minutes, seconds, frames;
        BMDTimecodeBCD bcd;
        bool dropFrame = false;

        if(ceil(fps) - fps > epsilon) { /* NTSCi drop framecode  */
                dropFrame = true;
        }

        tc->GetComponents (&hours, &minutes, &seconds, &frames);
        frames++;

        if((double) frames > fps - epsilon) {
                frames = 0;
                seconds++;
                if(seconds >= 60) {
                        seconds = 0;
                        minutes++;
                        if(dropFrame) {
                                if(minutes % 10 != 0)
                                        seconds = 2;
                        }
                        if(minutes >= 60) {
                                minutes = 0;
                                hours++;
                                if(hours >= 24) {
                                        hours = 0;
                                }
                        }
                }
        }

        bcd = (frames % 10) | (frames / 10) << 4 | (seconds % 10) << 8 | (seconds / 10) << 12 | (minutes % 10)  << 16 | (minutes / 10) << 20 |
                (hours % 10) << 24 | (hours / 10) << 28;

        tc->SetBCD(bcd);
}

int display_decklink_putf(void *state, char *frame)
{
        int tmp;
        struct state_decklink *s = (struct state_decklink *)state;
        struct timeval tv;

        UNUSED(frame);

        assert(s->magic == DECKLINK_MAGIC);

        gettimeofday(&tv, NULL);


        uint32_t i;
        s->state[0].deckLinkOutput->GetBufferedVideoFrameCount(&i);
        //if (i > 2) 
        if (0) 
                fprintf(stderr, "Frame dropped!\n");
        else {
                for (int j = 0; j < s->devices_cnt; ++j) {
                        if(s->emit_timecode) {
                                s->state[j].deckLinkFrame->SetTimecode(bmdVideoOutputRP188, s->timecode);
                        }

#ifdef DECKLINK_LOW_LATENCY
                        s->state[j].deckLinkOutput->DisplayVideoFrameSync(s->state[j].deckLinkFrame);
                        if(!s->fast) {
                                s->state[j].deckLinkFrame->Release();
                        }
#else
                        s->state[j].deckLinkOutput->ScheduleVideoFrame(s->state[j].deckLinkFrame,
                                        s->frames * s->frameRateDuration, s->frameRateDuration, s->frameRateScale);
#endif /* DECKLINK_LOW_LATENCY */

                        if(!s->fast) {
                                s->state[j].deckLinkFrame = NULL;
                        }
                }
                s->frames++;
                if(s->emit_timecode) {
                        update_timecode(s->timecode, s->frame->fps);
                }
        }


        gettimeofday(&tv, NULL);
        double seconds = tv_diff(tv, s->tv);
        if (seconds > 5) {
                double fps = (s->frames - s->frames_last) / seconds;
                fprintf(stdout, "[Decklink display] %lu frames in %g seconds = %g FPS\n",
                        s->frames - s->frames_last, seconds, fps);
                s->tv = tv;
                s->frames_last = s->frames;
        }

        return TRUE;
}

static BMDDisplayMode get_mode(IDeckLinkOutput *deckLinkOutput, struct video_desc desc, BMDTimeValue *frameRateDuration,
		BMDTimeScale        *frameRateScale)
{	IDeckLinkDisplayModeIterator     *displayModeIterator;
        IDeckLinkDisplayMode*             deckLinkDisplayMode;
        BMDDisplayMode			  displayMode = bmdModeUnknown;
        
        // Populate the display mode combo with a list of display modes supported by the installed DeckLink card
        if (FAILED(deckLinkOutput->GetDisplayModeIterator(&displayModeIterator)))
        {
                fprintf(stderr, "Fatal: cannot create display mode iterator [decklink].\n");
                return (BMDDisplayMode) -1;
        }

        while (displayModeIterator->Next(&deckLinkDisplayMode) == S_OK)
        {
                STRING modeNameString;
                const char *modeNameCString;
                if (deckLinkDisplayMode->GetName(&modeNameString) == S_OK)
                {
#ifdef HAVE_MACOSX
                        modeNameCString = (char *) malloc(128);
                        CFStringGetCString(modeNameString, (char *) modeNameCString, 128, kCFStringEncodingMacRoman);
#else
                        modeNameCString = modeNameString;
#endif
                        if (deckLinkDisplayMode->GetWidth() == desc.width &&
                                        deckLinkDisplayMode->GetHeight() == desc.height)
                        {
                                double displayFPS;
                                BMDFieldDominance dominance;
                                bool interlaced;

                                dominance = deckLinkDisplayMode->GetFieldDominance();
                                if (dominance == bmdLowerFieldFirst ||
                                                dominance == bmdUpperFieldFirst)
                                        interlaced = true;
                                else // progressive, psf, unknown
                                        interlaced = false;

                                deckLinkDisplayMode->GetFrameRate(frameRateDuration,
                                                frameRateScale);
                                displayFPS = (double) *frameRateScale / *frameRateDuration;
                                if(fabs(desc.fps - displayFPS) < 0.01 && (desc.interlacing == INTERLACED_MERGED ? interlaced : !interlaced)
                                  )
                                {
                                        printf("DeckLink - selected mode: %s\n", modeNameCString);
                                        displayMode = deckLinkDisplayMode->GetDisplayMode();
                                        break;
                                }
                        }
                }
        }
        displayModeIterator->Release();
        
        return displayMode;
}

int
display_decklink_reconfigure(void *state, struct video_desc desc)
{
        struct state_decklink            *s = (struct state_decklink *)state;
        
        bool                              modeFound = false;
        BMDDisplayMode                    displayMode;
        BMDDisplayModeSupport             supported;
        int h_align = 0;

        assert(s->magic == DECKLINK_MAGIC);
        
        s->frame->color_spec = desc.color_spec;
        s->frame->interlacing = desc.interlacing;
        s->frame->fps = desc.fps;

	switch (desc.color_spec) {
                case UYVY:
                        s->pixelFormat = bmdFormat8BitYUV;
                        break;
                case v210:
                        s->pixelFormat = bmdFormat10BitYUV;
                        break;
                case RGBA:
                        s->pixelFormat = bmdFormat8BitBGRA;
                        break;
                default:
                        fprintf(stderr, "[DeckLink] Unsupported pixel format!\n");
        }

        for(int i = 0; i < s->devices_cnt; ++i) {
                s->state[0].deckLinkOutput->StopScheduledPlayback (0, NULL, 0);
        }

	if(s->stereo) {
		for (int i = 0; i < 2; ++i) {
			struct tile  *tile = vf_get_tile(s->frame, i);
			tile->width = desc.width;
		        tile->height = desc.height;
	                tile->linesize = vc_get_linesize(tile->width, s->frame->color_spec);
	                tile->data_len = tile->linesize * tile->height;
	        }
		displayMode = get_mode(s->state[0].deckLinkOutput, desc, &s->frameRateDuration,
                                                &s->frameRateScale);
                if(displayMode == (BMDDisplayMode) -1)
                        goto error;
		
		s->state[0].deckLinkOutput->DoesSupportVideoMode(displayMode, s->pixelFormat, bmdVideoOutputDualStream3D,
	                                &supported, NULL);
                if(supported == bmdDisplayModeNotSupported)
                {
                        fprintf(stderr, "[decklink] Requested parameters combination not supported - %dx%d@%f.\n", desc.width, desc.height, (double)desc.fps);
                        goto error;
                }
                
                if(s->fast) {
                        if(s->state[0].deckLinkFrame) {
                                s->state[0].deckLinkFrame->Release();
                        }
                        s->state[0].deckLinkFrame = DeckLink3DFrame::Create(s->frame->tiles[0].width, s->frame->tiles[0].height,
                                        s->frame->tiles[0].linesize, s->pixelFormat);
                }

                s->state[0].deckLinkOutput->EnableVideoOutput(displayMode,  bmdVideoOutputDualStream3D);
#ifndef DECKLINK_LOW_LATENCY
                s->state[0].deckLinkOutput->StartScheduledPlayback(0, s->frameRateScale, (double) s->frameRateDuration);
#endif /* ! defined DECKLINK_LOW_LATENCY */
        } else {
                if(desc.tile_count > s->devices_cnt) {
                        fprintf(stderr, "[decklink] Expected at most %d streams. Got %d.\n", s->devices_cnt,
                                        desc.tile_count);
                        goto error;
                }

	        for(int i = 0; i < s->devices_cnt; ++i) {
                        BMDVideoOutputFlags outputFlags= bmdVideoOutputFlagDefault;
	                struct tile  *tile = vf_get_tile(s->frame, i);
	                
	                tile->width = desc.width;
	                tile->height = desc.height;
	                tile->linesize = vc_get_linesize(tile->width, s->frame->color_spec);
	                tile->data_len = tile->linesize * tile->height;
	                
	                displayMode = get_mode(s->state[i].deckLinkOutput, desc, &s->frameRateDuration,
                                                &s->frameRateScale);
                        if(displayMode == (BMDDisplayMode) -1)
                                goto error;

                        if(s->emit_timecode) {
                                outputFlags = bmdVideoOutputRP188;
                        }
	
	                s->state[i].deckLinkOutput->DoesSupportVideoMode(displayMode, s->pixelFormat, outputFlags,
	                                &supported, NULL);
	                if(supported == bmdDisplayModeNotSupported)
	                {
                                fprintf(stderr, "[decklink] Requested parameters "
                                                "combination not supported - %d * %dx%d@%f, timecode %s.\n",
                                                desc.tile_count, tile->width, tile->height, desc.fps,
                                                (outputFlags & bmdVideoOutputRP188 ? "ON": "OFF"));
	                        goto error;
	                }
                        if(s->fast) {
                                if(s->state[i].deckLinkFrame) {
                                        s->state[i].deckLinkFrame->Release();
                                }
                                s->state[i].deckLinkFrame = DeckLinkFrame::Create(s->frame->tiles[0].width, s->frame->tiles[0].height,
                                                s->frame->tiles[0].linesize, s->pixelFormat);
                        }

	
	                s->state[i].deckLinkOutput->EnableVideoOutput(displayMode, outputFlags);
	        }
	
	        for(int i = 0; i < s->devices_cnt; ++i) {
#ifndef DECKLINK_LOW_LATENCY
	                s->state[i].deckLinkOutput->StartScheduledPlayback(0, s->frameRateScale, (double) s->frameRateDuration);
#endif /* ! defined DECKLINK_LOW_LATENCY */
	        }
	}

        s->initialized = true;
        return TRUE;

error:
        return FALSE;
}

static int blackmagic_api_version_check(STRING *current_version)
{
        int ret = TRUE;
        *current_version = NULL;

        IDeckLinkAPIInformation *APIInformation = CreateDeckLinkAPIInformationInstance();
        if(APIInformation == NULL) {
                return FALSE;
        }
        int64_t value;
        HRESULT res;
        res = APIInformation->GetInt(BMDDeckLinkAPIVersion, &value);
        if(res != S_OK) {
                APIInformation->Release();
                return FALSE;
        }

        if(BLACKMAGIC_DECKLINK_API_VERSION > value) { // this is safe comparision, for internal structure please see SDK documentation
                APIInformation->GetString(BMDDeckLinkAPIVersion, current_version);
                ret  = FALSE;
        }


        APIInformation->Release();
        return ret;
}


void *display_decklink_init(char *fmt, unsigned int flags)
{
        struct state_decklink *s;
        IDeckLinkIterator*                              deckLinkIterator;
        HRESULT                                         result;
        int                                             cardIdx[MAX_DEVICES];
        int                                             dnum = 0;
        IDeckLinkConfiguration*         deckLinkConfiguration = NULL;
        // for Decklink Studio which has switchable XLR - analog 3 and 4 or AES/EBU 3,4 and 5,6
        BMDAudioOutputAnalogAESSwitch audioConnection = 0;
        BMDVideo3DPackingFormat HDMI3DPacking = 0;

        STRING current_version;
        if(!blackmagic_api_version_check(&current_version)) {
		fprintf(stderr, "\nThe DeckLink drivers may not be installed or are outdated.\n");
		fprintf(stderr, "This UltraGrid version was compiled against DeckLink drivers %s. You should have at least this version.\n\n",
                                BLACKMAGIC_DECKLINK_API_VERSION_STRING);
                fprintf(stderr, "Vendor download page is http://http://www.blackmagic-design.com/support/ \n");
                if(current_version) {
                        const char *currentVersionCString;
#ifdef HAVE_MACOSX
                        currentVersionCString = (char *) malloc(128);
                        CFStringGetCString(current_version, (char *) currentVersionCString, 128, kCFStringEncodingMacRoman);
#else
                        currentVersionCString = current_version;
#endif
                        fprintf(stderr, "Currently installed version is: %s\n", currentVersionCString);
#ifdef HAVE_MACOSX
                        CFRelease(current_version);
#endif
                        free((void *)currentVersionCString);
                } else {
                        fprintf(stderr, "No installed drivers detected\n");
                }
                fprintf(stderr, "\n");
                return NULL;
        }


        s = (struct state_decklink *)calloc(1, sizeof(struct state_decklink));
        s->magic = DECKLINK_MAGIC;
        s->stereo = FALSE;
        s->emit_timecode = false;
        s->link = LINK_UNSPECIFIED;

        s->fast = false;
        
        if(fmt == NULL) {
                cardIdx[0] = 0;
                s->devices_cnt = 1;
                fprintf(stderr, "Card number unset, using first found (see -d decklink:help)!\n");

        } else if (strcmp(fmt, "help") == 0) {
                show_help();
                return NULL;
        } else  {
                char *tmp = strdup(fmt);
                char *ptr;
                char *saveptr1 = 0ul, *saveptr2 = 0ul;

                ptr = strtok_r(tmp, ":", &saveptr1);                
                char *devices = strdup(ptr);
                s->devices_cnt = 0;
                ptr = strtok_r(devices, ",", &saveptr2);
                do {
                        cardIdx[s->devices_cnt] = atoi(ptr);
                        ++s->devices_cnt;
                } while ((ptr = strtok_r(NULL, ",", &saveptr2)));
                free(devices);
                
                while((ptr = strtok_r(NULL, ":", &saveptr1)))  {
                        if(strcasecmp(ptr, "3D") == 0) {
                                s->stereo = true;
                        } else if(strcasecmp(ptr, "timecode") == 0) {
                                s->emit_timecode = true;
                        } else if(strcasecmp(ptr, "3G") == 0) {
                                s->link = LINK_3G;
                        } else if(strcasecmp(ptr, "dual-link") == 0) {
                                s->link = LINK_DUAL;
                        } else if(strncasecmp(ptr, "HDMI3DPacking=", strlen("HDMI3DPacking=")) == 0) {
                                char *packing = ptr + strlen("HDMI3DPacking=");
                                if(strcasecmp(packing, "SideBySideHalf") == 0) {
                                        HDMI3DPacking = bmdVideo3DPackingSidebySideHalf;
                                } else if(strcasecmp(packing, "LineByLine") == 0) {
                                        HDMI3DPacking = bmdVideo3DPackingLinebyLine;
                                } else if(strcasecmp(packing, "TopAndBottom") == 0) {
                                        HDMI3DPacking = bmdVideo3DPackingTopAndBottom;
                                } else if(strcasecmp(packing, "FramePacking") == 0) {
                                        HDMI3DPacking = bmdVideo3DPackingFramePacking;
                                } else if(strcasecmp(packing, "LeftOnly") == 0) {
                                        HDMI3DPacking = bmdVideo3DPackingRightOnly;
                                } else if(strcasecmp(packing, "RightOnly") == 0) {
                                        HDMI3DPacking = bmdVideo3DPackingLeftOnly;
                                } else {
                                        fprintf(stderr, "[DeckLink] Warning: HDMI 3D packing %s.\n", packing);
                                }
                        } else if(strcasecmp(ptr, "fast") == 0) {
                                s->fast = true;
                        } else {
                                fprintf(stderr, "[DeckLink] Warning: unknown options in config string.\n");
                        }
                }
                free (tmp);
        }
	assert(!s->stereo || s->devices_cnt == 1);

        gettimeofday(&s->tv, NULL);

        // Initialize the DeckLink API
        deckLinkIterator = CreateDeckLinkIteratorInstance();
        if (!deckLinkIterator)
        {
		fprintf(stderr, "\nA DeckLink iterator could not be created. The DeckLink drivers may not be installed or are outdated.\n");
		fprintf(stderr, "This UltraGrid version was compiled with DeckLink drivers %s. You should have at least this version.\n\n",
                                BLACKMAGIC_DECKLINK_API_VERSION_STRING);
                return NULL;
        }

        for(int i = 0; i < s->devices_cnt; ++i) {
                s->state[i].delegate = NULL;
                s->state[i].deckLink = NULL;
                s->state[i].deckLinkOutput = NULL;
                s->state[i].deckLinkFrame = NULL;
                s->state[i].deckLinkConfiguration = NULL;
        }

        // Connect to the first DeckLink instance
        IDeckLink    *deckLink;
        while (deckLinkIterator->Next(&deckLink) == S_OK)
        {
                bool found = false;
                for(int i = 0; i < s->devices_cnt; ++i) {
                        if (dnum == cardIdx[i]){
                                s->state[i].deckLink = deckLink;
                                found = true;
                        }
                }
                if(!found && deckLink != NULL)
                        deckLink->Release();
                dnum++;
        }
        for(int i = 0; i < s->devices_cnt; ++i) {
                if(s->state[i].deckLink == NULL) {
                        fprintf(stderr, "No DeckLink PCI card #%d found\n", cardIdx[i]);
                        return NULL;
                }
        }

        if(flags & (DISPLAY_FLAG_AUDIO_EMBEDDED | DISPLAY_FLAG_AUDIO_AESEBU | DISPLAY_FLAG_AUDIO_ANALOG)) {
                s->play_audio = TRUE;
                switch(flags & (DISPLAY_FLAG_AUDIO_EMBEDDED | DISPLAY_FLAG_AUDIO_AESEBU | DISPLAY_FLAG_AUDIO_ANALOG)) {
                        case DISPLAY_FLAG_AUDIO_EMBEDDED:
                                audioConnection = 0;
                                break;
                        case DISPLAY_FLAG_AUDIO_AESEBU:
                                audioConnection = bmdAudioOutputSwitchAESEBU;
                                break;
                        case DISPLAY_FLAG_AUDIO_ANALOG:
                                audioConnection = bmdAudioOutputSwitchAnalog;
                                break;
                        default:
                                fprintf(stderr, "[Decklink display] [Fatal] Unsupporetd audio connection.\n");
                                abort();
                }
                s->audio.data = NULL;
        } else {
                s->play_audio = FALSE;
        }
        
        if(s->stereo) {
        	s->frame = vf_alloc(2);
	} else {
		s->frame = vf_alloc(s->devices_cnt);
	}

        if(s->emit_timecode) {
                s->timecode = new DeckLinkTimecode;
        } else {
                s->timecode = NULL;
        }
        
        for(int i = 0; i < s->devices_cnt; ++i) {
                // Obtain the audio/video output interface (IDeckLinkOutput)
                if (s->state[i].deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&s->state[i].deckLinkOutput) != S_OK) {
                        if(s->state[i].deckLinkOutput != NULL)
                                s->state[i].deckLinkOutput->Release();
                        s->state[i].deckLink->Release();
                        return NULL;
                }

                // Query the DeckLink for its configuration interface
                result = s->state[i].deckLink->QueryInterface(IID_IDeckLinkConfiguration, (void**)&deckLinkConfiguration);
                s->state[i].deckLinkConfiguration = deckLinkConfiguration;
                if (result != S_OK)
                {
                        printf("Could not obtain the IDeckLinkConfiguration interface: %08x\n", (int) result);
                        return NULL;
                }

#ifdef DECKLINK_LOW_LATENCY
                HRESULT res = deckLinkConfiguration->SetFlag(bmdDeckLinkConfigLowLatencyVideoOutput, true);
                if(res != S_OK) {
                        fprintf(stderr, "[DeckLink display] Unable to set to low-latency mode.\n");
                }
#endif /* DECKLINK_LOW_LATENCY */

                if(HDMI3DPacking != 0) {
                        HRESULT res = deckLinkConfiguration->SetInt(bmdDeckLinkConfigHDMI3DPackingFormat,
                                        HDMI3DPacking);
                        if(res != S_OK) {
                                fprintf(stderr, "[DeckLink display] Unable set 3D packing.\n");
                        }
                }

                if(s->link != LINK_UNSPECIFIED) {
                        HRESULT res = deckLinkConfiguration->SetFlag(bmdDeckLinkConfig3GBpsVideoOutput,
                                        s->link == LINK_3G ? true : false);
                        if(res != S_OK) {
                                fprintf(stderr, "[DeckLink display] Unable set output SDI standard.\n");
                        }
                }

                if(s->play_audio == FALSE || i != 0) { //TODO: figure out output from multiple streams
                                s->state[i].deckLinkOutput->DisableAudioOutput();
                } else {
                        /* Actually no action is required to set audio connection because Blackmagic card plays audio through all its outputs (AES/SDI/analog) ....
                         */
                        printf("[Decklink playback] Audio output set to: ");
                        switch(audioConnection) {
                                case 0:
                                        printf("embedded");
                                        break;
                                case bmdAudioOutputSwitchAESEBU:
                                        printf("AES/EBU");
                                        break;
                                case bmdAudioOutputSwitchAnalog:
                                        printf("analog");
                                        break;
                        }
                        printf(".\n");
                         /*
                          * .... one exception is a card that has switchable cables between AES/EBU and analog. (But this applies only for channels 3 and above.)
                         */
                        if (audioConnection != 0) { // not embedded 
                                HRESULT res = deckLinkConfiguration->SetInt(bmdDeckLinkConfigAudioOutputAESAnalogSwitch,
                                                audioConnection);
                                if(res == S_OK) { // has switchable channels
                                        printf("[Decklink playback] Card with switchable audio channels detected. Switched to correct format.\n");
                                } else if(res == E_NOTIMPL) {
                                        // normal case - without switchable channels
                                } else {
                                        fprintf(stderr, "[Decklink playback] Unable to switch audio output for channels 3 or above although \n"
                                                        "card shall support it. Check if it is ok. Continuing anyway.\n");
                                }
                        }
                }

                s->state[i].delegate = new PlaybackDelegate(s, i);
                // Provide this class as a delegate to the audio and video output interfaces
#ifndef DECKLINK_LOW_LATENCY
                if(!s->fast) {
                        s->state[i].deckLinkOutput->SetScheduledFrameCompletionCallback(s->state[i].delegate);
                }
#endif /* ! defined DECKLINK_LOW_LATENCY */
                //s->state[i].deckLinkOutput->DisableAudioOutput();
        }

        s->frames = 0;
        s->initialized = false;

        return (void *)s;
}

void display_decklink_run(void *state)
{
        UNUSED(state);
}

void display_decklink_finish(void *state)
{
        UNUSED(state);
}

void display_decklink_done(void *state)
{
        debug_msg("display_decklink_done\n"); /* TOREMOVE */
        struct state_decklink *s = (struct state_decklink *)state;
        HRESULT result;

        assert (s != NULL);

        delete s->timecode;

        for (int i = 0; i < s->devices_cnt; ++i)
        {
                result = s->state[i].deckLinkOutput->StopScheduledPlayback (0, NULL, 0);
                if (result != S_OK) {
                        fprintf(stderr, MODULE_NAME "Cannot stop playback: %08x\n", (int) result);
                }

                if(s->play_audio && i == 0) {
                        result = s->state[i].deckLinkOutput->DisableAudioOutput();
                        if (result != S_OK) {
                                fprintf(stderr, MODULE_NAME "Could disable audio output: %08x\n", (int) result);
                        }
                }
                result = s->state[i].deckLinkOutput->DisableVideoOutput();
                if (result != S_OK) {
                        fprintf(stderr, MODULE_NAME "Could disable video output: %08x\n", (int) result);
                }

                if(s->state[i].deckLinkConfiguration != NULL) {
                        s->state[i].deckLinkConfiguration->Release();
                }

                if(s->state[i].deckLinkOutput != NULL) {
                        s->state[i].deckLinkOutput->Release();
                }

                if(s->state[i].deckLinkFrame != NULL) {
                        s->state[i].deckLinkFrame->Release();
                }

                if(s->state[i].deckLink != NULL) {
                        s->state[i].deckLink->Release();
                }

                if(s->state[i].delegate != NULL) {
                        delete s->state[i].delegate;
                }
        }

        vf_free(s->frame);
        free(s);
}

display_type_t *display_decklink_probe(void)
{
        display_type_t *dtype;

        dtype = (display_type_t *) malloc(sizeof(display_type_t));
        if (dtype != NULL) {
                dtype->id = DISPLAY_DECKLINK_ID;
                dtype->name = "decklink";
                dtype->description = "Blackmagick DeckLink card";
        }
        return dtype;
}

int display_decklink_get_property(void *state, int property, void *val, size_t *len)
{
        struct state_decklink *s = (struct state_decklink *)state;
        codec_t codecs[] = {v210, UYVY, RGBA};
        
        switch (property) {
                case DISPLAY_PROPERTY_CODECS:
                        if(sizeof(codecs) <= *len) {
                                memcpy(val, codecs, sizeof(codecs));
                        } else {
                                return FALSE;
                        }
                        
                        *len = sizeof(codecs);
                        break;
                case DISPLAY_PROPERTY_RSHIFT:
                        *(int *) val = 16;
                        *len = sizeof(int);
                        break;
                case DISPLAY_PROPERTY_GSHIFT:
                        *(int *) val = 8;
                        *len = sizeof(int);
                        break;
                case DISPLAY_PROPERTY_BSHIFT:
                        *(int *) val = 0;
                        *len = sizeof(int);
                        break;
                case DISPLAY_PROPERTY_BUF_PITCH:
                        *(int *) val = PITCH_DEFAULT;
                        *len = sizeof(int);
                        break;
                case DISPLAY_PROPERTY_VIDEO_MODE:
                        if(s->devices_cnt == 1 && !s->stereo)
                                *(int *) val = DISPLAY_PROPERTY_VIDEO_MERGED;
                        else
                                *(int *) val = DISPLAY_PROPERTY_VIDEO_SEPARATE_TILES;
                        break;

                default:
                        return FALSE;
        }
        return TRUE;
}

PlaybackDelegate::PlaybackDelegate (struct state_decklink * owner, int index) 
        : s(owner), i(index)
{
}

HRESULT         PlaybackDelegate::ScheduledFrameCompleted (IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result) 
{
	completedFrame->Release();
        return S_OK;
}

HRESULT         PlaybackDelegate::ScheduledPlaybackHasStopped ()
{
        return S_OK;
}

/*
 * AUDIO
 */
struct audio_frame * display_decklink_get_audio_frame(void *state)
{
        struct state_decklink *s = (struct state_decklink *)state;
        
        if(!s->play_audio)
                return NULL;
        return &s->audio;
}

void display_decklink_put_audio_frame(void *state, struct audio_frame *frame)
{
        struct state_decklink *s = (struct state_decklink *)state;
        unsigned int sampleFrameCount = s->audio.data_len / (s->audio.bps *
                        s->audio.ch_count);
        unsigned int sampleFramesWritten;

        /* we got probably count that cannot be played directly (probably 1) */
        if(s->output_audio_channel_count != s->audio.ch_count) {
                assert(s->audio.ch_count == 1); /* only reasonable value so far */
                if (sampleFrameCount * s->output_audio_channel_count 
                                * frame->bps > frame->max_size) {
                        fprintf(stderr, "[decklink] audio buffer overflow!\n");
                        sampleFrameCount = frame->max_size / 
                                        (s->output_audio_channel_count * frame->bps);
                        frame->data_len = sampleFrameCount *
                                        (frame->ch_count * frame->bps);
                }
                
                audio_frame_multiply_channel(frame,
                                s->output_audio_channel_count);
        }
        
	s->state[0].deckLinkOutput->ScheduleAudioSamples (s->audio.data, sampleFrameCount, 0, 		
                0, &sampleFramesWritten);
        if(sampleFramesWritten != sampleFrameCount)
                fprintf(stderr, "[decklink] audio buffer underflow!\n");

}

int display_decklink_reconfigure_audio(void *state, int quant_samples, int channels,
                int sample_rate) {
        struct state_decklink *s = (struct state_decklink *)state;
        BMDAudioSampleType sample_type;

        if(s->audio.data != NULL)
                free(s->audio.data);
                
        s->audio.bps = quant_samples / 8;
        s->audio.sample_rate = sample_rate;
        s->output_audio_channel_count = s->audio.ch_count = channels;
        
        if (s->audio.ch_count != 1 &&
                        s->audio.ch_count != 2 && s->audio.ch_count != 8 &&
                        s->audio.ch_count != 16) {
                fprintf(stderr, "[decklink] requested channel count isn't supported: "
                        "%d\n", s->audio.ch_count);
                s->play_audio = FALSE;
                return FALSE;
        }
        
        /* toggle one channel to supported two */
        if(s->audio.ch_count == 1) {
                 s->output_audio_channel_count = 2;
        }
        
        if((quant_samples != 16 && quant_samples != 32) ||
                        sample_rate != 48000) {
                fprintf(stderr, "[decklink] audio format isn't supported: "
                        "samples: %d, sample rate: %d\n",
                        quant_samples, sample_rate);
                s->play_audio = FALSE;
                return FALSE;
        }
        switch(quant_samples) {
                case 16:
                        sample_type = bmdAudioSampleType16bitInteger;
                        break;
                case 32:
                        sample_type = bmdAudioSampleType32bitInteger;
                        break;
                default:
                        return FALSE;
        }
                        
        s->state[0].deckLinkOutput->EnableAudioOutput(bmdAudioSampleRate48kHz,
                        sample_type,
                        s->output_audio_channel_count,
                        bmdAudioOutputStreamContinuous);
        s->state[0].deckLinkOutput->StartScheduledPlayback(0, s->frameRateScale, s->frameRateDuration);
        
        s->audio.max_size = 5 * (quant_samples / 8) 
                        * s->audio.ch_count
                        * sample_rate;                
        s->audio.data = (char *) malloc (s->audio.max_size);

        return TRUE;
}

bool operator==(const REFIID & first, const REFIID & second){
    return (first.byte0 == second.byte0) &&
        (first.byte1 == second.byte1) &&
        (first.byte2 == second.byte2) &&
        (first.byte3 == second.byte3) &&
        (first.byte4 == second.byte4) &&
        (first.byte5 == second.byte5) &&
        (first.byte6 == second.byte6) &&
        (first.byte7 == second.byte7) &&
        (first.byte8 == second.byte8) &&
        (first.byte9 == second.byte9) &&
        (first.byte10 == second.byte10) &&
        (first.byte11 == second.byte11) &&
        (first.byte12 == second.byte12) &&
        (first.byte13 == second.byte13) &&
        (first.byte14 == second.byte14) &&
        (first.byte15 == second.byte15);
}

HRESULT DeckLinkFrame::QueryInterface(REFIID id, void**frame)
{
        return E_NOINTERFACE;
}

ULONG DeckLinkFrame::AddRef()
{
        return ++ref;
}

ULONG DeckLinkFrame::Release()
{
        if(--ref == 0)
                delete this;
	return ref;
}

DeckLinkFrame::DeckLinkFrame(long w, long h, long rb, BMDPixelFormat pf)
	: width(w), height(h), rawBytes(rb), pixelFormat(pf), ref(1l)
{
        data = new char[rb * h];
        timecode = NULL;
}

DeckLinkFrame *DeckLinkFrame::Create(long width, long height, long rawBytes, BMDPixelFormat pixelFormat)
{
        return new DeckLinkFrame(width, height, rawBytes, pixelFormat);
}


DeckLinkFrame::~DeckLinkFrame() 
{
	delete[] data;
}

long DeckLinkFrame::GetWidth ()
{
        return width;
}

long DeckLinkFrame::GetHeight ()
{
        return height;
}

long DeckLinkFrame::GetRowBytes ()
{
        return rawBytes;
}

BMDPixelFormat DeckLinkFrame::GetPixelFormat ()
{
        return pixelFormat;
}

BMDFrameFlags DeckLinkFrame::GetFlags ()
{
        return bmdFrameFlagDefault;
}

HRESULT DeckLinkFrame::GetBytes (/* out */ void **buffer)
{
        *buffer = static_cast<void *>(data);
        return S_OK;
}

HRESULT DeckLinkFrame::GetTimecode (/* in */ BMDTimecodeFormat format, /* out */ IDeckLinkTimecode **timecode)
{
        *timecode = dynamic_cast<IDeckLinkTimecode *>(this->timecode);
        return S_OK;
}

HRESULT DeckLinkFrame::GetAncillaryData (/* out */ IDeckLinkVideoFrameAncillary **ancillary)
{
	return S_FALSE;
}

/* IDeckLinkMutableVideoFrame */
HRESULT DeckLinkFrame::SetFlags (/* in */ BMDFrameFlags newFlags)
{
        return E_FAIL;
}

HRESULT DeckLinkFrame::SetTimecode (/* in */ BMDTimecodeFormat format, /* in */ IDeckLinkTimecode *timecode)
{
        if(this->timecode)
                this->timecode->Release();
        this->timecode = timecode;
        return S_OK;
}

HRESULT DeckLinkFrame::SetTimecodeFromComponents (/* in */ BMDTimecodeFormat format, /* in */ uint8_t hours, /* in */ uint8_t minutes, /* in */ uint8_t seconds, /* in */ uint8_t frames, /* in */ BMDTimecodeFlags flags)
{
        return E_FAIL;
}

HRESULT DeckLinkFrame::SetAncillaryData (/* in */ IDeckLinkVideoFrameAncillary *ancillary)
{
        return E_FAIL;
}

HRESULT DeckLinkFrame::SetTimecodeUserBits (/* in */ BMDTimecodeFormat format, /* in */ BMDTimecodeUserBits userBits)
{
        return E_FAIL;
}



DeckLink3DFrame::DeckLink3DFrame(long w, long h, long rb, BMDPixelFormat pf) 
        : DeckLinkFrame(w, h, rb, pf), ref(1l)
{
        rightEye = DeckLinkFrame::Create(w, h, rb, pf);        
}

DeckLink3DFrame *DeckLink3DFrame::Create(long width, long height, long rawBytes, BMDPixelFormat pixelFormat)
{
        DeckLink3DFrame *frame = new DeckLink3DFrame(width, height, rawBytes, pixelFormat);
        return frame;
}

DeckLink3DFrame::~DeckLink3DFrame()
{
	rightEye->Release();
}

ULONG DeckLink3DFrame::AddRef()
{
        return DeckLinkFrame::AddRef();
}

ULONG DeckLink3DFrame::Release()
{
        return DeckLinkFrame::Release();
}

HRESULT DeckLink3DFrame::QueryInterface(REFIID id, void**frame)
{
        HRESULT result = E_NOINTERFACE;

        if(id == IID_IDeckLinkVideoFrame3DExtensions)
        {
                this->AddRef();
                *frame = dynamic_cast<IDeckLinkVideoFrame3DExtensions *>(this);
                result = S_OK;
        }
        return result;
}


BMDVideo3DPackingFormat DeckLink3DFrame::Get3DPackingFormat()
{
        return bmdVideo3DPackingLeftOnly;
}

HRESULT DeckLink3DFrame::GetFrameForRightEye(IDeckLinkVideoFrame ** frame) 
{
        *frame = rightEye;
        rightEye->AddRef();
        return S_OK;
}
