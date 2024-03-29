/*
 * QCP format (.qcp) demuxer
 * Copyright (c) 2009 Kenan Gillet
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
 * @file
 * QCP format (.qcp) demuxer
 * @author Kenan Gillet
 * @sa RFC 3625: "The QCP File Format and Media Types for Speech Data"
 *     http://tools.ietf.org/html/rfc3625
 */

#include "../config.h"

#include "../libavutil/intreadwrite.h"
#include "avformat.h"

typedef struct {
    uint32_t data_size;                     ///< size of data chunk

#define QCP_MAX_MODE 4
    int16_t rates_per_mode[QCP_MAX_MODE+1]; ///< contains the packet size corresponding
                                            ///< to each mode, -1 if no size.
} QCPContext;

/**
 * Last 15 out of 16 bytes of QCELP-13K GUID, as stored in the file;
 * the first byte of the GUID can be either 0x41 or 0x42.
 */
static const uint8_t guid_qcelp_13k_part[15] = {
    0x6d, 0x7f, 0x5e, 0x15, 0xb1, 0xd0, 0x11, 0xba,
    0x91, 0x00, 0x80, 0x5f, 0xb4, 0xb9, 0x7e
};

/**
 * EVRC GUID as stored in the file
 */
static const uint8_t guid_evrc[16] = {
    0x8d, 0xd4, 0x89, 0xe6, 0x76, 0x90, 0xb5, 0x46,
    0x91, 0xef, 0x73, 0x6a, 0x51, 0x00, 0xce, 0xb4
};

/**
 * SMV GUID as stored in the file
 */
static const uint8_t guid_smv[16] = {
    0x75, 0x2b, 0x7c, 0x8d, 0x97, 0xa7, 0x49, 0xed,
    0x98, 0x5e, 0xd5, 0x3c, 0x8c, 0xc7, 0x5f, 0x84
};

/**
 * @param guid contains at least 16 bytes
 * @return 1 if the guid is a qcelp_13k guid, 0 otherwise
 */
static int is_qcelp_13k_guid(const uint8_t *guid) {
    return (guid[0] == 0x41 || guid[0] == 0x42)
        && !memcmp(guid+1, guid_qcelp_13k_part, sizeof(guid_qcelp_13k_part));
}

static int qcp_probe(AVProbeData *pd)
{
    if (AV_RL32(pd->buf  ) == AV_RL32("RIFF") &&
        AV_RL64(pd->buf+8) == AV_RL64("QLCMfmt "))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int qcp_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    QCPContext    *c  = s->priv_data;
    AVStream      *st = av_new_stream(s, 0);
    uint8_t       buf[16];
    int           i, nb_rates;

    if (!st)
        return AVERROR(ENOMEM);

    get_be32(pb);                    // "RIFF"
    s->file_size = get_le32(pb) + 8;
    url_fskip(pb, 8 + 4 + 1 + 1);    // "QLCMfmt " + chunk-size + major-version + minor-version

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->channels   = 1;
    get_buffer(pb, buf, 16);
    if (is_qcelp_13k_guid(buf)) {
        st->codec->codec_id = CODEC_ID_QCELP;
    } else if (!memcmp(buf, guid_evrc, 16)) {
        av_log(s, AV_LOG_ERROR, "EVRC codec is not supported.\n");
        return AVERROR_PATCHWELCOME;
    } else if (!memcmp(buf, guid_smv, 16)) {
        av_log(s, AV_LOG_ERROR, "SMV codec is not supported.\n");
        return AVERROR_PATCHWELCOME;
    } else {
        av_log(s, AV_LOG_ERROR, "Unknown codec GUID.\n");
        return AVERROR_INVALIDDATA;
    }
    url_fskip(pb, 2 + 80); // codec-version + codec-name
    st->codec->bit_rate = get_le16(pb);

    s->packet_size = get_le16(pb);
    url_fskip(pb, 2); // block-size
    st->codec->sample_rate = get_le16(pb);
    url_fskip(pb, 2); // sample-size

    memset(c->rates_per_mode, -1, sizeof(c->rates_per_mode));
    nb_rates = get_le32(pb);
    nb_rates = FFMIN(nb_rates, 8);
    for (i=0; i<nb_rates; i++) {
        int size = get_byte(pb);
        int mode = get_byte(pb);
        if (mode > QCP_MAX_MODE) {
            av_log(s, AV_LOG_WARNING, "Unknown entry %d=>%d in rate-map-table.\n ", mode, size);
        } else
            c->rates_per_mode[mode] = size;
    }
    url_fskip(pb, 16 - 2*nb_rates + 20); // empty entries of rate-map-table + reserved

    return 0;
}

static int qcp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    QCPContext    *c  = s->priv_data;
    unsigned int  chunk_size, tag;

    while(!url_feof(pb)) {
        if (c->data_size) {
            int pkt_size, ret, mode = get_byte(pb);

            if (s->packet_size) {
                pkt_size = s->packet_size - 1;
            } else if (mode > QCP_MAX_MODE || (pkt_size = c->rates_per_mode[mode]) < 0) {
                c->data_size--;
                continue;
            }

            if (c->data_size <= pkt_size) {
                av_log(s, AV_LOG_WARNING, "Data chunk is too small.\n");
                pkt_size = c->data_size - 1;
            }

            if ((ret = av_get_packet(pb, pkt, pkt_size)) >= 0) {
                if (pkt_size != ret)
                    av_log(s, AV_LOG_ERROR, "Packet size is too small.\n");

                c->data_size -= pkt_size + 1;
            }
            return ret;
        }

        if (url_ftell(pb) & 1 && get_byte(pb))
            av_log(s, AV_LOG_WARNING, "Padding should be 0.\n");

        tag        = get_le32(pb);
        chunk_size = get_le32(pb);
        switch (tag) {
        case MKTAG('v', 'r', 'a', 't'):
            if (get_le32(pb)) // var-rate-flag
                s->packet_size = 0;
            url_fskip(pb, 4); // size-in-packets
            break;
        case MKTAG('d', 'a', 't', 'a'):
            c->data_size = chunk_size;
            break;

        default:
            url_fskip(pb, chunk_size);
        }
    }
    return AVERROR_EOF;
}

AVInputFormat qcp_demuxer = {
    .name           = "qcp",
    .long_name      = NULL_IF_CONFIG_SMALL("QCP format"),
    .priv_data_size = sizeof(QCPContext),
    .read_probe     = qcp_probe,
    .read_header    = qcp_read_header,
    .read_packet    = qcp_read_packet,
};
