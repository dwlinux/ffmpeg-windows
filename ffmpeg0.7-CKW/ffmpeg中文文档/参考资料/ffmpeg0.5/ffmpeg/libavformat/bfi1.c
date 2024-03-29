/*
 * Brute Force & Ignorance (BFI) demuxer
 * Copyright (c) 2008 Sisir Koppaka
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file bfi.c
 * @brief Brute Force & Ignorance (.bfi) file demuxer
 * @author Sisir Koppaka ( sisir.koppaka at gmail dot com )
 * @sa http://wiki.multimedia.cx/index.php?title=BFI
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

typedef struct BFIContext {
    int nframes;
    int audio_frame;
    int video_frame;
    int video_size;
    int avflag;
} BFIContext;

static int bfi_probe(AVProbeData * p)
{
    /* Check file header */
    if (AV_RL32(p->buf) == MKTAG('B', 'F', '&', 'I'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int bfi_read_header(AVFormatContext * s, AVFormatParameters * ap)
{
    BFIContext *bfi = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *vstream;
    AVStream *astream;
    int fps, chunk_header;

    /* Initialize the video codec... */
    vstream = av_new_stream(s, 0);
    if (!vstream)
        return AVERROR(ENOMEM);

    /* Initialize the audio codec... */
    astream = av_new_stream(s, 0);
    if (!astream)
        return AVERROR(ENOMEM);

    /* Set the total number of frames. */
    url_fskip(pb, 8);
    chunk_header           = get_le32(pb);
    bfi->nframes           = get_le32(pb);
    get_le32(pb);
    get_le32(pb);
    get_le32(pb);
    fps                    = get_le32(pb);
    url_fskip(pb, 12);
    vstream->codec->width  = get_le32(pb);
    vstream->codec->height = get_le32(pb);

    /*Load the palette to extradata */
    url_fskip(pb, 8);
    vstream->codec->extradata      = av_malloc(768);
    vstream->codec->extradata_size = 768;
    get_buffer(pb, vstream->codec->extradata,
               vstream->codec->extradata_size);

    astream->codec->sample_rate = get_le32(pb);

    /* Set up the video codec... */
    av_set_pts_info(vstream, 32, 1, fps);
    vstream->codec->codec_type = CODEC_TYPE_VIDEO;
    vstream->codec->codec_id   = CODEC_ID_BFI;
    vstream->codec->pix_fmt    = PIX_FMT_PAL8;

    /* Set up the audio codec now... */
    astream->codec->codec_type      = CODEC_TYPE_AUDIO;
    astream->codec->codec_id        = CODEC_ID_PCM_U8;
    astream->codec->channels        = 1;
    astream->codec->bits_per_coded_sample = 8;
    astream->codec->bit_rate        =
        astream->codec->sample_rate * astream->codec->bits_per_coded_sample;
    url_fseek(pb, chunk_header - 3, SEEK_SET);
    av_set_pts_info(astream, 64, 1, astream->codec->sample_rate);
    return 0;
}


static int bfi_read_packet(AVFormatContext * s, AVPacket * pkt)
{
    BFIContext *bfi = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret, audio_offset, video_offset, chunk_size, audio_size = 0;
    if (bfi->nframes == 0 || url_feof(pb)) {
        return AVERROR(EIO);
    }

    /* If all previous chunks were completely read, then find a new one... */
    if (!bfi->avflag) {
        uint32_t state = 0;
        while(state != MKTAG('S','A','V','I')){
            if (url_feof(pb))
                return AVERROR(EIO);
            state = 256*state + get_byte(pb);
        }
        /* Now that the chunk's location is confirmed, we proceed... */
        chunk_size      = get_le32(pb);
        get_le32(pb);
        audio_offset    = get_le32(pb);
        get_le32(pb);
        video_offset    = get_le32(pb);
        audio_size      = video_offset - audio_offset;
        bfi->video_size = chunk_size - video_offset;

        //Tossing an audio packet at the audio decoder.
        ret = av_get_packet(pb, pkt, audio_size);
        if (ret < 0)
            return ret;

        pkt->pts          = bfi->audio_frame;
        bfi->audio_frame += ret;
    }

    else {

        //Tossing a video packet at the video decoder.
        ret = av_get_packet(pb, pkt, bfi->video_size);
        if (ret < 0)
            return ret;

        pkt->pts          = bfi->video_frame;
        bfi->video_frame += ret / bfi->video_size;

        /* One less frame to read. A cursory decrement. */
        bfi->nframes--;
    }

    bfi->avflag       = !bfi->avflag;
    pkt->stream_index = bfi->avflag;
    return ret;
}

AVInputFormat bfi_demuxer = {
    "bfi",
    NULL_IF_CONFIG_SMALL("Brute Force & Ignorance"),
    sizeof(BFIContext),
    bfi_probe,
    bfi_read_header,
    bfi_read_packet,
};
