#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"


typedef struct TransformContext {
    const AVClass *class;
    int backUp;
    //add some private data if you want
} TransformContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static void image_copy_plane(uint8_t *dst, int dst_linesize,
                         const uint8_t *src, int src_linesize,
                         int bytewidth, int height)
{
    if (!dst || !src)
        return;
    av_assert0(abs(src_linesize) >= bytewidth);
    av_assert0(abs(dst_linesize) >= bytewidth);
    for (;height > 0; height--) {
        memcpy(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }
}

//for YUV data, frame->data[0] save Y, frame->data[1] save U, frame->data[2] save V
static int frame_copy_video(AVFrame *dst, const AVFrame *src)
{
    int i, planes;

    if (dst->width  > src->width ||
        dst->height > src->height)
        return AVERROR(EINVAL);

    planes = av_pix_fmt_count_planes(dst->format);
    //make sure data is valid
    for (i = 0; i < planes; i++)
        if (!dst->data[i] || !src->data[i])
            return AVERROR(EINVAL);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dst->format);
    int planes_nb = 0;
    for (i = 0; i < desc->nb_components; i++)
        planes_nb = FFMAX(planes_nb, desc->comp[i].plane + 1);

    for (i = 0; i < planes_nb; i++) {
        int h = dst->height;
        int bwidth = av_image_get_linesize(dst->format, dst->width, i);
        if (bwidth < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_image_get_linesize failed\n");
            return;
        }
        if (i == 1 || i == 2) {
            h = AV_CEIL_RSHIFT(dst->height, desc->log2_chroma_h);
        }
        image_copy_plane(dst->data[i], dst->linesize[i],
                            src->data[i], src->linesize[i],
                            bwidth, h);
    }
    return 0;
}

/**************************************************************************
* you can modify this function, do what you want here. use src frame, and blend to dst frame.
* for this demo, we just copy some part of src frame to dst frame(out_w = in_w/2, out_h = in_h/2)
***************************************************************************/
static int do_conversion(AVFilterContext *ctx, void *arg, int jobnr,
                        int nb_jobs)
{
    TransformContext *privCtx = ctx->priv;
    ThreadData *td = arg;
    AVFrame *dst = td->out;
    AVFrame *src = td->in;

    frame_copy_video(dst, src);
    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    //av_log(NULL, AV_LOG_WARNING, "### chenxf filter_frame, link %x, frame %x \n", link, in);
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    AVFrame *out;
    av_log(NULL, AV_LOG_WARNING, "----%d\n", link->format);

    //allocate a new buffer, data is null
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    //the new output frame, property is the same as input frame, only width/height is different
    av_frame_copy_props(out, in);
    out->width  = outlink->w;
    out->height = outlink->h;

    ThreadData td;
    td.in = in;
    td.out = out;
    int res;
    if(res = avctx->internal->execute(avctx, do_conversion, &td, NULL, FFMIN(outlink->h, avctx->graph->nb_threads))) {
        return res;
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TransformContext *privCtx = ctx->priv;

    //you can modify output width/height here
    outlink->w = ctx->inputs[0]->w/2;
    outlink->h = ctx->inputs[0]->h/2;
    av_log(NULL, AV_LOG_DEBUG, "configure output, w h = (%d %d), format %d \n", outlink->w, outlink->h, outlink->format);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    av_log(NULL, AV_LOG_DEBUG, "init \n");
    TransformContext *privCtx = ctx->priv;
    //init something here if you want
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    av_log(NULL, AV_LOG_DEBUG, "uninit \n");
    TransformContext *privCtx = ctx->priv;
    //uninit something here if you want
}

//currently we just support the most common YUV420, can add more if needed
static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}


//*************
#define OFFSET(x) offsetof(TransformContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption transform_options[] = {
    { "backUp",         "a backup parameters, NOT use so far",          OFFSET(backUp),    AV_OPT_TYPE_STRING, {.str = "0"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { NULL }

};// TODO: add something if needed

static const AVClass transform_class = {
    .class_name       = "transform",
    .item_name        = av_default_item_name,
    .option           = transform_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad avfilter_vf_transform_inputs[] = {
    {
        .name         = "transform_inputpad",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_transform_outputs[] = {
    {
        .name = "transform_outputpad",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_transform = {
    .name           = "transform",
    .description    = NULL_IF_CONFIG_SMALL("cut a part of video"),
    .priv_size      = sizeof(TransformContext),
    .priv_class     = &transform_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs         = avfilter_vf_transform_inputs,
    .outputs        = avfilter_vf_transform_outputs,
};