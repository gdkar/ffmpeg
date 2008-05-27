/*
 * Flash Compatible Streaming Format muxer
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2003 Tinic Uro.
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

#include "libavcodec/bitstream.h"
#include "avformat.h"
#include "swf.h"

static void put_swf_tag(AVFormatContext *s, int tag)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;

    swf->tag_pos = url_ftell(pb);
    swf->tag = tag;
    /* reserve some room for the tag */
    if (tag & TAG_LONG) {
        put_le16(pb, 0);
        put_le32(pb, 0);
    } else {
        put_le16(pb, 0);
    }
}

static void put_swf_end_tag(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;
    offset_t pos;
    int tag_len, tag;

    pos = url_ftell(pb);
    tag_len = pos - swf->tag_pos - 2;
    tag = swf->tag;
    url_fseek(pb, swf->tag_pos, SEEK_SET);
    if (tag & TAG_LONG) {
        tag &= ~TAG_LONG;
        put_le16(pb, (tag << 6) | 0x3f);
        put_le32(pb, tag_len - 4);
    } else {
        assert(tag_len < 0x3f);
        put_le16(pb, (tag << 6) | tag_len);
    }
    url_fseek(pb, pos, SEEK_SET);
}

static inline void max_nbits(int *nbits_ptr, int val)
{
    int n;

    if (val == 0)
        return;
    val = abs(val);
    n = 1;
    while (val != 0) {
        n++;
        val >>= 1;
    }
    if (n > *nbits_ptr)
        *nbits_ptr = n;
}

static void put_swf_rect(ByteIOContext *pb,
                         int xmin, int xmax, int ymin, int ymax)
{
    PutBitContext p;
    uint8_t buf[256];
    int nbits, mask;

    init_put_bits(&p, buf, sizeof(buf));

    nbits = 0;
    max_nbits(&nbits, xmin);
    max_nbits(&nbits, xmax);
    max_nbits(&nbits, ymin);
    max_nbits(&nbits, ymax);
    mask = (1 << nbits) - 1;

    /* rectangle info */
    put_bits(&p, 5, nbits);
    put_bits(&p, nbits, xmin & mask);
    put_bits(&p, nbits, xmax & mask);
    put_bits(&p, nbits, ymin & mask);
    put_bits(&p, nbits, ymax & mask);

    flush_put_bits(&p);
    put_buffer(pb, buf, pbBufPtr(&p) - p.buf);
}

static void put_swf_line_edge(PutBitContext *pb, int dx, int dy)
{
    int nbits, mask;

    put_bits(pb, 1, 1); /* edge */
    put_bits(pb, 1, 1); /* line select */
    nbits = 2;
    max_nbits(&nbits, dx);
    max_nbits(&nbits, dy);

    mask = (1 << nbits) - 1;
    put_bits(pb, 4, nbits - 2); /* 16 bits precision */
    if (dx == 0) {
        put_bits(pb, 1, 0);
        put_bits(pb, 1, 1);
        put_bits(pb, nbits, dy & mask);
    } else if (dy == 0) {
        put_bits(pb, 1, 0);
        put_bits(pb, 1, 0);
        put_bits(pb, nbits, dx & mask);
    } else {
        put_bits(pb, 1, 1);
        put_bits(pb, nbits, dx & mask);
        put_bits(pb, nbits, dy & mask);
    }
}

#define FRAC_BITS 16

static void put_swf_matrix(ByteIOContext *pb,
                           int a, int b, int c, int d, int tx, int ty)
{
    PutBitContext p;
    uint8_t buf[256];
    int nbits;

    init_put_bits(&p, buf, sizeof(buf));

    put_bits(&p, 1, 1); /* a, d present */
    nbits = 1;
    max_nbits(&nbits, a);
    max_nbits(&nbits, d);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, a);
    put_bits(&p, nbits, d);

    put_bits(&p, 1, 1); /* b, c present */
    nbits = 1;
    max_nbits(&nbits, c);
    max_nbits(&nbits, b);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, c);
    put_bits(&p, nbits, b);

    nbits = 1;
    max_nbits(&nbits, tx);
    max_nbits(&nbits, ty);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, tx);
    put_bits(&p, nbits, ty);

    flush_put_bits(&p);
    put_buffer(pb, buf, pbBufPtr(&p) - p.buf);
}

static int swf_write_header(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc, *audio_enc, *video_enc;
    PutBitContext p;
    uint8_t buf1[256];
    int i, width, height, rate, rate_base;
    int is_avm2;

    swf->audio_in_pos = 0;
    swf->sound_samples = 0;
    swf->swf_frame_number = 0;
    swf->video_frame_number = 0;

    video_enc = NULL;
    audio_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            if (enc->codec_id == CODEC_ID_MP3) {
                if (!enc->frame_size) {
                    av_log(s, AV_LOG_ERROR, "audio frame size not set\n");
                    return -1;
                }
                audio_enc = enc;
            } else {
                av_log(s, AV_LOG_ERROR, "SWF muxer only supports MP3\n");
                return -1;
            }
        } else {
            if (enc->codec_id == CODEC_ID_VP6F ||
                enc->codec_id == CODEC_ID_FLV1 ||
                enc->codec_id == CODEC_ID_MJPEG) {
                video_enc = enc;
            } else {
                av_log(s, AV_LOG_ERROR, "SWF muxer only supports VP6, FLV1 and MJPEG\n");
                return -1;
            }
        }
    }

    if (!video_enc) {
        /* currently, cannot work correctly if audio only */
        swf->video_type = 0;
        width = 320;
        height = 200;
        rate = 10;
        rate_base= 1;
    } else {
        swf->video_type = video_enc->codec_id;
        width = video_enc->width;
        height = video_enc->height;
        rate = video_enc->time_base.den;
        rate_base = video_enc->time_base.num;
    }

    if (!audio_enc) {
        swf->audio_type = 0;
        swf->samples_per_frame = (44100. * rate_base) / rate;
    } else {
        swf->audio_type = audio_enc->codec_id;
        swf->samples_per_frame = (audio_enc->sample_rate * rate_base) / rate;
    }

    is_avm2 = !strcmp("avm2", s->oformat->name);

    put_tag(pb, "FWS");
    if (is_avm2)
        put_byte(pb, 9);
    else if (video_enc && video_enc->codec_id == CODEC_ID_VP6F)
        put_byte(pb, 8); /* version (version 8 and above support VP6 codec) */
    else if (video_enc && video_enc->codec_id == CODEC_ID_FLV1)
        put_byte(pb, 6); /* version (version 6 and above support FLV1 codec) */
    else
        put_byte(pb, 4); /* version (should use 4 for mpeg audio support) */
    put_le32(pb, DUMMY_FILE_SIZE); /* dummy size
                                      (will be patched if not streamed) */

    put_swf_rect(pb, 0, width * 20, 0, height * 20);
    put_le16(pb, (rate * 256) / rate_base); /* frame rate */
    swf->duration_pos = url_ftell(pb);
    put_le16(pb, (uint16_t)(DUMMY_DURATION * (int64_t)rate / rate_base)); /* frame count */

    /* avm2/swf v9 (also v8?) files require a file attribute tag */
    if (is_avm2) {
        put_swf_tag(s, TAG_FILEATTRIBUTES);
        put_le32(pb, 1<<3); /* set ActionScript v3/AVM2 flag */
        put_swf_end_tag(s);
    }

    /* define a shape with the jpeg inside */
    if (video_enc && video_enc->codec_id == CODEC_ID_MJPEG) {
        put_swf_tag(s, TAG_DEFINESHAPE);

        put_le16(pb, SHAPE_ID); /* ID of shape */
        /* bounding rectangle */
        put_swf_rect(pb, 0, width, 0, height);
        /* style info */
        put_byte(pb, 1); /* one fill style */
        put_byte(pb, 0x41); /* clipped bitmap fill */
        put_le16(pb, BITMAP_ID); /* bitmap ID */
        /* position of the bitmap */
        put_swf_matrix(pb, (int)(1.0 * (1 << FRAC_BITS)), 0,
                       0, (int)(1.0 * (1 << FRAC_BITS)), 0, 0);
        put_byte(pb, 0); /* no line style */

        /* shape drawing */
        init_put_bits(&p, buf1, sizeof(buf1));
        put_bits(&p, 4, 1); /* one fill bit */
        put_bits(&p, 4, 0); /* zero line bit */

        put_bits(&p, 1, 0); /* not an edge */
        put_bits(&p, 5, FLAG_MOVETO | FLAG_SETFILL0);
        put_bits(&p, 5, 1); /* nbits */
        put_bits(&p, 1, 0); /* X */
        put_bits(&p, 1, 0); /* Y */
        put_bits(&p, 1, 1); /* set fill style 1 */

        /* draw the rectangle ! */
        put_swf_line_edge(&p, width, 0);
        put_swf_line_edge(&p, 0, height);
        put_swf_line_edge(&p, -width, 0);
        put_swf_line_edge(&p, 0, -height);

        /* end of shape */
        put_bits(&p, 1, 0); /* not an edge */
        put_bits(&p, 5, 0);

        flush_put_bits(&p);
        put_buffer(pb, buf1, pbBufPtr(&p) - p.buf);

        put_swf_end_tag(s);
    }

    if (audio_enc && audio_enc->codec_id == CODEC_ID_MP3) {
        int v;

        /* start sound */
        put_swf_tag(s, TAG_STREAMHEAD2);

        v = 0;
        switch(audio_enc->sample_rate) {
        case 11025:
            v |= 1 << 2;
            break;
        case 22050:
            v |= 2 << 2;
            break;
        case 44100:
            v |= 3 << 2;
            break;
        default:
            /* not supported */
            av_log(s, AV_LOG_ERROR, "swf does not support that sample rate, choose from (44100, 22050, 11025).\n");
            return -1;
        }
        v |= 0x02; /* 16 bit playback */
        if (audio_enc->channels == 2)
            v |= 0x01; /* stereo playback */
        put_byte(s->pb, v);
        v |= 0x20; /* mp3 compressed */
        put_byte(s->pb, v);
        put_le16(s->pb, swf->samples_per_frame);  /* avg samples per frame */
        put_le16(s->pb, 0);

        put_swf_end_tag(s);
    }

    put_flush_packet(s->pb);
    return 0;
}

static int swf_write_video(AVFormatContext *s,
                           AVCodecContext *enc, const uint8_t *buf, int size)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;

    /* Flash Player limit */
    if (swf->swf_frame_number == 16000)
        av_log(enc, AV_LOG_INFO, "warning: Flash Player limit of 16000 frames reached\n");

    if (swf->video_type == CODEC_ID_VP6F ||
        swf->video_type == CODEC_ID_FLV1) {
        if (swf->video_frame_number == 0) {
            /* create a new video object */
            put_swf_tag(s, TAG_VIDEOSTREAM);
            put_le16(pb, VIDEO_ID);
            put_le16(pb, 15000); /* hard flash player limit */
            put_le16(pb, enc->width);
            put_le16(pb, enc->height);
            put_byte(pb, 0);
            put_byte(pb,codec_get_tag(swf_codec_tags,swf->video_type));
            put_swf_end_tag(s);

            /* place the video object for the first time */
            put_swf_tag(s, TAG_PLACEOBJECT2);
            put_byte(pb, 0x36);
            put_le16(pb, 1);
            put_le16(pb, VIDEO_ID);
            put_swf_matrix(pb, 1 << FRAC_BITS, 0, 0, 1 << FRAC_BITS, 0, 0);
            put_le16(pb, swf->video_frame_number);
            put_byte(pb, 'v');
            put_byte(pb, 'i');
            put_byte(pb, 'd');
            put_byte(pb, 'e');
            put_byte(pb, 'o');
            put_byte(pb, 0x00);
            put_swf_end_tag(s);
        } else {
            /* mark the character for update */
            put_swf_tag(s, TAG_PLACEOBJECT2);
            put_byte(pb, 0x11);
            put_le16(pb, 1);
            put_le16(pb, swf->video_frame_number);
            put_swf_end_tag(s);
        }

        /* set video frame data */
        put_swf_tag(s, TAG_VIDEOFRAME | TAG_LONG);
        put_le16(pb, VIDEO_ID);
        put_le16(pb, swf->video_frame_number++);
        put_buffer(pb, buf, size);
        put_swf_end_tag(s);
    } else if (swf->video_type == CODEC_ID_MJPEG) {
        if (swf->swf_frame_number > 0) {
            /* remove the shape */
            put_swf_tag(s, TAG_REMOVEOBJECT);
            put_le16(pb, SHAPE_ID); /* shape ID */
            put_le16(pb, 1); /* depth */
            put_swf_end_tag(s);

            /* free the bitmap */
            put_swf_tag(s, TAG_FREECHARACTER);
            put_le16(pb, BITMAP_ID);
            put_swf_end_tag(s);
        }

        put_swf_tag(s, TAG_JPEG2 | TAG_LONG);

        put_le16(pb, BITMAP_ID); /* ID of the image */

        /* a dummy jpeg header seems to be required */
        put_byte(pb, 0xff);
        put_byte(pb, 0xd8);
        put_byte(pb, 0xff);
        put_byte(pb, 0xd9);
        /* write the jpeg image */
        put_buffer(pb, buf, size);

        put_swf_end_tag(s);

        /* draw the shape */

        put_swf_tag(s, TAG_PLACEOBJECT);
        put_le16(pb, SHAPE_ID); /* shape ID */
        put_le16(pb, 1); /* depth */
        put_swf_matrix(pb, 20 << FRAC_BITS, 0, 0, 20 << FRAC_BITS, 0, 0);
        put_swf_end_tag(s);
    }

    swf->swf_frame_number ++;

    /* streaming sound always should be placed just before showframe tags */
    if (swf->audio_type && swf->audio_in_pos) {
        put_swf_tag(s, TAG_STREAMBLOCK | TAG_LONG);
        put_le16(pb, swf->sound_samples);
        put_le16(pb, 0); // seek samples
        put_buffer(pb, swf->audio_fifo, swf->audio_in_pos);
        put_swf_end_tag(s);

        /* update FIFO */
        swf->sound_samples = 0;
        swf->audio_in_pos = 0;
    }

    /* output the frame */
    put_swf_tag(s, TAG_SHOWFRAME);
    put_swf_end_tag(s);

    put_flush_packet(s->pb);

    return 0;
}

static int swf_write_audio(AVFormatContext *s,
                           AVCodecContext *enc, const uint8_t *buf, int size)
{
    SWFContext *swf = s->priv_data;

    /* Flash Player limit */
    if (swf->swf_frame_number == 16000)
        av_log(enc, AV_LOG_INFO, "warning: Flash Player limit of 16000 frames reached\n");

    if (swf->audio_in_pos + size >= AUDIO_FIFO_SIZE) {
        av_log(s, AV_LOG_ERROR, "audio fifo too small to mux audio essence\n");
        return -1;
    }

    memcpy(swf->audio_fifo +  swf->audio_in_pos, buf, size);
    swf->audio_in_pos += size;
    swf->sound_samples += enc->frame_size;

    /* if audio only stream make sure we add swf frames */
    if (swf->video_type == 0)
        swf_write_video(s, enc, 0, 0);

    return 0;
}

static int swf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    if (codec->codec_type == CODEC_TYPE_AUDIO)
        return swf_write_audio(s, codec, pkt->data, pkt->size);
    else
        return swf_write_video(s, codec, pkt->data, pkt->size);
}

static int swf_write_trailer(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc, *video_enc;
    int file_size, i;

    video_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_VIDEO)
            video_enc = enc;
    }

    put_swf_tag(s, TAG_END);
    put_swf_end_tag(s);

    put_flush_packet(s->pb);

    /* patch file size and number of frames if not streamed */
    if (!url_is_streamed(s->pb) && video_enc) {
        file_size = url_ftell(pb);
        url_fseek(pb, 4, SEEK_SET);
        put_le32(pb, file_size);
        url_fseek(pb, swf->duration_pos, SEEK_SET);
        put_le16(pb, video_enc->frame_number);
        url_fseek(pb, file_size, SEEK_SET);
    }
    return 0;
}

#ifdef CONFIG_SWF_MUXER
AVOutputFormat swf_muxer = {
    "swf",
    "Flash format",
    "application/x-shockwave-flash",
    "swf",
    sizeof(SWFContext),
    CODEC_ID_MP3,
    CODEC_ID_FLV1,
    swf_write_header,
    swf_write_packet,
    swf_write_trailer,
};
#endif
#ifdef CONFIG_AVM2_MUXER
AVOutputFormat avm2_muxer = {
    "avm2",
    "Flash 9 (AVM2) format",
    "application/x-shockwave-flash",
    NULL,
    sizeof(SWFContext),
    CODEC_ID_MP3,
    CODEC_ID_FLV1,
    swf_write_header,
    swf_write_packet,
    swf_write_trailer,
};
#endif
