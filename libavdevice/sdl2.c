/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * libSDL output device
 */

#include <SDL2/SDL.h>

#include "libavutil/avstring.h"
#include "libavutil/atomic.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;
    SDL_Window  *window;
    SDL_Renderer*render;
    SDL_Texture *texture;
    char *window_title;
    char *icon_title;
    int window_width;
    int window_height;  /**< size of the window */
    int window_fullscreen;

    SDL_Rect overlay_rect;
    int overlay_fmt;

    int sdl_was_already_inited;
    SDL_Thread *event_thread;
    SDL_mutex  *mutex;
    SDL_cond   *init_cond;
    int init_status; /* return code used to signal initialization errors */
    int init_done;
    int quit;
} SDL2Context;

static const struct sdl2_overlay_pix_fmt_entry {
    enum AVPixelFormat pix_fmt; int overlay_fmt;
} sdl2_overlay_pix_fmt_map[] = {
    { AV_PIX_FMT_ARGB,    SDL_PIXELFORMAT_ARGB8888},
    { AV_PIX_FMT_RGBA,    SDL_PIXELFORMAT_RGBA8888},
    { AV_PIX_FMT_RGB0,    SDL_PIXELFORMAT_RGBX8888},
    { AV_PIX_FMT_BGRA,    SDL_PIXELFORMAT_BGRA8888},
    { AV_PIX_FMT_BGR0,    SDL_PIXELFORMAT_BGRX8888},
    { AV_PIX_FMT_BGR24,   SDL_PIXELFORMAT_BGR888},
    { AV_PIX_FMT_RGB24,   SDL_PIXELFORMAT_RGB888},
    { AV_PIX_FMT_RGB24,   SDL_PIXELFORMAT_RGB888},
    { AV_PIX_FMT_UYVY422, SDL_PIXELFORMAT_UYVY},
    { AV_PIX_FMT_YUV444P, SDL_PIXELFORMAT_IYUV},
    { AV_PIX_FMT_YVYU422, SDL_PIXELFORMAT_YVYU},
    { AV_PIX_FMT_NV12,    SDL_PIXELFORMAT_NV12},
    { AV_PIX_FMT_NV21,    SDL_PIXELFORMAT_NV21},
    { AV_PIX_FMT_NONE,    0                },
};

static int sdl2_writetrailer(AVFormatContext *s)
{
    SDL2Context *sdl2 = s->priv_data;

    avpriv_atomic_int_set(&sdl2->quit,1);

    if (sdl2->texture)
        SDL_DestroyTexture(sdl2->texture);
    sdl2->texture = NULL;
    if (sdl2->render)
        SDL_DestroyRenderer(sdl2->render);
    sdl2->render = NULL;
    if (sdl2->window)
        SDL_DestroyWindow(sdl2->window);
    sdl2->window = NULL;
    sdl2->texture = NULL;
    if (sdl2->event_thread)
        SDL_WaitThread(sdl2->event_thread, NULL);
    sdl2->event_thread = NULL;
    if (sdl2->mutex)
        SDL_DestroyMutex(sdl2->mutex);
    sdl2->mutex = NULL;
    if (sdl2->init_cond)
        SDL_DestroyCond(sdl2->init_cond);
    sdl2->init_cond = NULL;

    if (!sdl2->sdl_was_already_inited)
        SDL_Quit();

    return 0;
}

static void compute_overlay_rect(AVFormatContext *s)
{
    AVRational sar, dar; /* sample and display aspect ratios */
    SDL2Context *sdl2      = s->priv_data;
    AVStream *st           = s->streams[0];
    AVCodecParameters *par = st->codecpar;
    SDL_Rect *overlay_rect = &sdl2->overlay_rect;

    /* compute overlay width and height from the codec context information */
    sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : (AVRational){ 1, 1 };
    dar = av_mul_q(sar, (AVRational){ par->width, par->height });

    /* we suppose the screen has a 1/1 sample aspect ratio */
    if (sdl2->window_width && sdl2->window_height) {
        /* fit in the window */
        if (av_cmp_q(dar, (AVRational){ sdl2->window_width, sdl2->window_height }) > 0) {
            /* fit in width */
            overlay_rect->w = sdl2->window_width;
            overlay_rect->h = av_rescale(overlay_rect->w, dar.den, dar.num);
        } else {
            /* fit in height */
            overlay_rect->h = sdl2->window_height;
            overlay_rect->w = av_rescale(overlay_rect->h, dar.num, dar.den);
        }
    } else {
        if (sar.num > sar.den) {
            overlay_rect->w = par->width;
            overlay_rect->h = av_rescale(overlay_rect->w, dar.den, dar.num);
        } else {
            overlay_rect->h = par->height;
            overlay_rect->w = av_rescale(overlay_rect->h, dar.num, dar.den);
        }
        sdl2->window_width  = overlay_rect->w;
        sdl2->window_height = overlay_rect->h;
    }

    overlay_rect->x = (sdl2->window_width  - overlay_rect->w) / 2;
    overlay_rect->y = (sdl2->window_height - overlay_rect->h) / 2;
}

#define SDL_BASE_FLAGS (SDL_WINDOW_RESIZABLE)

static int event_thread(void *arg)
{
    AVFormatContext *s = arg;
    SDL2Context *sdl2 = s->priv_data;
    int flags = SDL_WINDOW_RESIZABLE | (sdl2->window_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    AVStream *st = s->streams[0];
    AVCodecParameters *par = st->codecpar;

    /* initialization */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        av_log(s, AV_LOG_ERROR, "Unable to initialize SDL: %s\n", SDL_GetError());
        sdl2->init_status = AVERROR(EINVAL);
        goto init_end;
    }
    sdl2->window = SDL_CreateWindow(
            sdl2->window_title,
            SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
            sdl2->window_width, sdl2->window_height,
            flags);
    if(!sdl2->window) {
        av_log(sdl2, AV_LOG_ERROR, "Unable to create window: %s\n", SDL_GetError());
        sdl2->init_status = AVERROR(EINVAL);
        goto init_end;
    }
    sdl2->render = SDL_CreateRenderer(sdl2->window, -1, SDL_RENDERER_TARGETTEXTURE);
    if(!sdl2->render) {
        av_log(sdl2, AV_LOG_ERROR, "Unable to create renderer: %s\n", SDL_GetError());
        sdl2->init_status = AVERROR(EINVAL);
        goto init_end;
    }
    compute_overlay_rect(s);
    sdl2->texture = SDL_CreateTexture(sdl2->render,sdl2->overlay_fmt,
            SDL_TEXTUREACCESS_TARGET, sdl2->overlay_rect.w, sdl2->overlay_rect.h);

    if(!sdl2->texture) {
        av_log(sdl2, AV_LOG_ERROR, "Unable to create texture: %s\n", SDL_GetError());
        sdl2->init_status = AVERROR(EINVAL);
        goto init_end;
    }

    sdl2->init_status = 0;
    av_log(s, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s -> w:%d h:%d\n",
           par->width, par->height, av_get_pix_fmt_name(par->format),
           sdl2->overlay_rect.w, sdl2->overlay_rect.h);

init_end:
    SDL_LockMutex(sdl2->mutex);
    sdl2->init_done = 1;
    SDL_UnlockMutex(sdl2->mutex);
    SDL_CondSignal(sdl2->init_cond);

    if (sdl2->init_status < 0) {
        if(sdl2->texture) SDL_DestroyTexture(sdl2->texture);
        sdl2->texture = NULL;
        if(sdl2->render)  SDL_DestroyRenderer(sdl2->render);
        sdl2->render = NULL;
        if(sdl2->window)  SDL_DestroyWindow(sdl2->window);
        sdl2->window = NULL;
        return sdl2->init_status;
    }

    /* event loop */
    while (!avpriv_atomic_int_get(&sdl2->quit)) {
        int ret = 0;
        SDL_Event event;
        SDL_PumpEvents();
        ret = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Error when getting SDL event: %s\n", SDL_GetError());
            continue;
        }
        if (ret == 0) {
            SDL_Delay(10);
            continue;
        }
        switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                avpriv_atomic_int_set(&sdl2->quit,1);
                break;
            }
            break;
        case SDL_QUIT:
            avpriv_atomic_int_set(&sdl2->quit,1);
            break;

        case SDL_WINDOWEVENT:
            switch(event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                {

                    SDL_LockMutex(sdl2->mutex);
                    sdl2->window_width  = event.window.data1;
                    sdl2->window_height = event.window.data2;
                    compute_overlay_rect(s);
                    SDL_DestroyTexture(sdl2->texture);
                    sdl2->texture = SDL_CreateTexture(sdl2->render,sdl2->overlay_fmt,
                        SDL_TEXTUREACCESS_TARGET, sdl2->overlay_rect.w, sdl2->overlay_rect.h);

                    if (!sdl2->texture) {
                        av_log(s, AV_LOG_ERROR, "Failed to set SDL video mode: %s\n", SDL_GetError());
                        avpriv_atomic_int_set(&sdl2->quit,1);
                    } else {
                        compute_overlay_rect(s);
                    }
                    SDL_UnlockMutex(sdl2->mutex);
                    break;
                }
                default:
                    break;
            }
            break;

        default:
            break;
        }
    }

    return 0;
}

static int sdl2_writeheader(AVFormatContext *s)
{
    SDL2Context *sdl2 = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecParameters *par = st->codecpar;
    int i, ret;

    if (!sdl2->window_title)
        sdl2->window_title = av_strdup(s->filename);
    if (!sdl2->icon_title)
        sdl2->icon_title = av_strdup(sdl2->window_title);

    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        av_log(s, AV_LOG_ERROR, "SDL video subsystem was already inited, aborting\n");
        sdl2->sdl_was_already_inited = 1;
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (   s->nb_streams > 1
        || par->codec_type != AVMEDIA_TYPE_VIDEO
        || par->codec_id   != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    for (i = 0; sdl2_overlay_pix_fmt_map[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (sdl2_overlay_pix_fmt_map[i].pix_fmt == par->format) {
            sdl2->overlay_fmt = sdl2_overlay_pix_fmt_map[i].overlay_fmt;
            break;
        }
    }

    if (!sdl2->overlay_fmt) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', choose one of yuv420p, yuyv422, or uyvy422\n",
               av_get_pix_fmt_name(par->format));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    /* compute overlay width and height from the codec context information */
    compute_overlay_rect(s);

    sdl2->init_cond = SDL_CreateCond();
    if (!sdl2->init_cond) {
        av_log(s, AV_LOG_ERROR, "Could not create SDL condition variable: %s\n", SDL_GetError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    sdl2->mutex = SDL_CreateMutex();
    if (!sdl2->mutex) {
        av_log(s, AV_LOG_ERROR, "Could not create SDL mutex: %s\n", SDL_GetError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    sdl2->event_thread = SDL_CreateThread(event_thread, "sdl2 outdev thread",s);
    if (!sdl2->event_thread) {
        av_log(s, AV_LOG_ERROR, "Could not create SDL event thread: %s\n", SDL_GetError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    /* wait until the video system has been inited */
    SDL_LockMutex(sdl2->mutex);
    while (!sdl2->init_done) {
        SDL_CondWait(sdl2->init_cond, sdl2->mutex);
    }
    SDL_UnlockMutex(sdl2->mutex);
    if (sdl2->init_status < 0) {
        ret = sdl2->init_status;
        goto fail;
    }
    return 0;

fail:
    sdl2_writetrailer(s);
    return ret;
}

static int sdl2_writepacket(AVFormatContext *s, AVPacket *pkt)
{
    SDL2Context *sdl2 = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    uint8_t *pointers[4] = { NULL};
    int      linesize[4] = { 0 };
    int      planes = 0;
    if (sdl2->quit) {
        sdl2_writetrailer(s);
        return AVERROR(EIO);
    }
    av_image_fill_arrays(pointers, linesize, pkt->data, par->format, par->width, par->height, 1);
    
    SDL_LockMutex(sdl2->mutex);
    planes = av_pix_fmt_count_planes(sdl2->overlay_fmt);
    if(planes > 1) {
        SDL_UpdateYUVTexture(
                sdl2->texture,
                NULL,
                pointers[0], linesize[0],
                pointers[1], linesize[1],
                pointers[2], linesize[2]);
    }else{
        SDL_UpdateTexture(
                sdl2->texture,NULL,
                pointers[0], linesize[0]);
}
    SDL_SetRenderDrawColor(sdl2->render, 0, 0, 0, 1.);
    SDL_RenderClear(sdl2->render);
    SDL_RenderCopy(sdl2->render, sdl2->texture, NULL, &sdl2->overlay_rect);
    SDL_RenderPresent(sdl2->render);
    SDL_UnlockMutex(sdl2->mutex);

    return 0;
}

#define OFFSET(x) offsetof(SDL2Context,x)

static const AVOption options[] = {
    { "window_title", "set SDL2 window title",           OFFSET(window_title), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "icon_title",   "set SDL2 iconified window title", OFFSET(icon_title)  , AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_size",  "set SDL2 window forced size",     OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_fullscreen", "set SDL2 window fullscreen", OFFSET(window_fullscreen), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass sdl2_class = {
    .class_name = "sdl2 outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_sdl2_muxer = {
    .name           = "sdl2",
    .long_name      = NULL_IF_CONFIG_SMALL("SDL2 output device"),
    .priv_data_size = sizeof(SDL2Context),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = sdl2_writeheader,
    .write_packet   = sdl2_writepacket,
    .write_trailer  = sdl2_writetrailer,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &sdl2_class,
};
