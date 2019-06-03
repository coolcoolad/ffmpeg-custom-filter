/*
 * Copyright (c) 2007 Bobby Bingham
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
 * video vertical flip filter
 */

#include <stdio.h>
#include "config.h"
#if HAVE_OPENCV2_CORE_CORE_C_H
#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#else
#include <opencv/cv.h>
#include <opencv/cxcore.h>
#endif
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"
#include "formats.h"

typedef struct MyFlipContext {
    const AVClass *class;
    int vsub;   ///< vertical chroma subsampling
} MyFlipContext;

static const AVOption myflip_options[] = {
    { NULL }
};

AVFILTER_DEFINE_CLASS(myflip);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_BGR24, AV_PIX_FMT_BGRA, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static void fill_iplimage_from_frame(IplImage *img, const AVFrame *frame, enum AVPixelFormat pixfmt)
{
    IplImage *tmpimg;
    int depth, channels_nb;

    if      (pixfmt == AV_PIX_FMT_GRAY8) { depth = IPL_DEPTH_8U;  channels_nb = 1; }
    else if (pixfmt == AV_PIX_FMT_BGRA)  { depth = IPL_DEPTH_8U;  channels_nb = 4; }
    else if (pixfmt == AV_PIX_FMT_BGR24) { depth = IPL_DEPTH_8U;  channels_nb = 3; }
    else {
        av_log(NULL, AV_LOG_WARNING, "----not support format: %d\n", pixfmt);
        return;
    }

    tmpimg = cvCreateImageHeader((CvSize){frame->width, frame->height}, depth, channels_nb);
    *img = *tmpimg;
    img->imageData = img->imageDataOrigin = frame->data[0];
    img->dataOrder = IPL_DATA_ORDER_PIXEL;
    img->origin    = IPL_ORIGIN_TL;
    img->widthStep = frame->linesize[0];
}

static void fill_frame_from_iplimage(AVFrame *frame, const IplImage *img, enum AVPixelFormat pixfmt)
{
    frame->linesize[0] = img->widthStep;
    frame->data[0]     = img->imageData;
}

// static int filter_frame(AVFilterLink *link, AVFrame *frame)
// {
//     MyFlipContext *flip = link->dst->priv;
//     int i;

//     for (i = 0; i < 4; i ++) {
//         int vsub = i == 1 || i == 2 ? flip->vsub : 0;
//         int height = AV_CEIL_RSHIFT(link->h, vsub);

//         if (frame->data[i]) {
//             frame->data[i] += (height - 1) * frame->linesize[i];
//             frame->linesize[i] = -frame->linesize[i];
//         }
//     }

//     return ff_filter_frame(link->dst->outputs[0], frame);
// }

// static int filter_frame(AVFilterLink *inlink, AVFrame *in)
// {
//     AVFilterContext *ctx = inlink->dst;
//     AVFilterLink *outlink= inlink->dst->outputs[0];
//     AVFrame *out;
//     IplImage inimg, outimg;

//     out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
//     if (!out) {
//         av_frame_free(&in);
//         return AVERROR(ENOMEM);
//     }
//     av_frame_copy_props(out, in);

//     fill_iplimage_from_frame(&inimg , in , inlink->format);
//     fill_iplimage_from_frame(&outimg, out, inlink->format);
//     fill_frame_from_iplimage(out, &inimg, inlink->format);

//     av_frame_free(&in);

//     return ff_filter_frame(outlink, out);
// }

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterLink *outlink= inlink->dst->outputs[0];
    AVFrame *out;
    IplImage inimg, outimg;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    out->width  = outlink->w;
    out->height = outlink->h;

    fill_iplimage_from_frame(&inimg , in , inlink->format);
    fill_iplimage_from_frame(&outimg, out, inlink->format);

    IplImage* gray = cvCreateImage(cvGetSize(&inimg), IPL_DEPTH_8U, 1); 
    cvCvtColor(&inimg, gray, CV_BGR2GRAY);
    cvSmooth(gray, gray, CV_GAUSSIAN, 7, 7, 0, 0); 

    //IplImage* cc_img = cvCreateImage(cvGetSize(gray), gray->depth, 3); 
    cvSetZero(&outimg);
    CvScalar(ext_color);

    cvCanny(gray, gray, 50, 200, 3); 

    CvMemStorage *mem;
    mem = cvCreateMemStorage(0);
    CvSeq *contours = 0;
    int n = cvFindContours(gray, mem, &contours, sizeof(CvContour), CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0,0));
    contours = cvApproxPoly( contours, sizeof(CvContour), mem, CV_POLY_APPROX_DP, 3, 1 );

    for (; contours != 0; contours = contours->h_next)
    {
        //if (contours->total == 4) {
            ext_color = CV_RGB( rand()&255, rand()&255, rand()&255 ); //randomly coloring different contours
            cvDrawContours(&outimg, contours, ext_color, CV_RGB(0,0,0), -1, CV_FILLED, 8, cvPoint(0,0));
        //}
    }

    fill_frame_from_iplimage(out, &outimg, inlink->format);

    av_frame_free(&in);
    cvReleaseMemStorage( &mem );
    cvReleaseImage( &gray );

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_myflip_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_myflip_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_myflip = {
    .name        = "myflip",
    .description = NULL_IF_CONFIG_SMALL("MyFlip the input video vertically."),
    .priv_size   = sizeof(MyFlipContext),
    .priv_class  = &myflip_class,
    .query_formats = query_formats,
    .inputs      = avfilter_vf_myflip_inputs,
    .outputs     = avfilter_vf_myflip_outputs,
    .flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
