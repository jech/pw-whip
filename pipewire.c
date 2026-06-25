#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <opus.h>

#include "pipewire.h"

// The Opus frame size, in ms.
#define FRAME_SIZE_MS 20

// The maximum number of samples we need to be able to buffer in order to
// accumulate an Opus frame.
#define MAXBUFFERED (FRAME_SIZE_MS * 48000 * 4 / 1000)

struct data {
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct spa_audio_info format;

    OpusEncoder *encoder;
    float buffered[MAXBUFFERED];
    int numbuffered;
    uint32_t timestamp;
    int bitrate;

    sendfunction send;
    void *send_closure;
};

static void buffer(struct data *data, const float *samples, int n)
{
    if(data->numbuffered + n > MAXBUFFERED)
        abort();
    memcpy(data->buffered + data->numbuffered, samples, n * sizeof(float));
    data->numbuffered += n;
}

static void on_process(void *closure)
{
    struct data *data = closure;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    float *samples;

    b = pw_stream_dequeue_buffer(data->stream);
    if(b == NULL) {
        pw_log_warn("out of buffers");
        return;
    }

    buf = b->buffer;
    samples = (float*)buf->datas[0].data;
    if(samples == NULL) {
        pw_stream_queue_buffer(data->stream, b);
        return;
    }

    if(data->encoder == NULL) {
        pw_stream_queue_buffer(data->stream, b);
        return;
    }

    int channels = data->format.info.raw.channels;
    int new_samples = buf->datas[0].chunk->size / sizeof(float);
    int frame_samples = FRAME_SIZE_MS * data->format.info.raw.rate * channels / 1000;
    if(frame_samples > MAXBUFFERED)
        frame_samples = MAXBUFFERED;

    unsigned char opus[1024];
    int start = 0;
    while(1) {
        int count;
        if(data->numbuffered > 0) {
            int more = frame_samples - data->numbuffered;
            if(new_samples - start < more)
                break;
            buffer(data, samples + start, more);
            start += more;
            count = opus_encode_float(data->encoder,
                                      data->buffered,
                                      data->numbuffered / channels,
                                      opus, sizeof(opus));
            data->numbuffered = 0;
        } else {
            if(new_samples - start < frame_samples)
                break;
            count = opus_encode_float(data->encoder,
                                      samples + start, frame_samples / channels,
                                      opus, sizeof(opus));
            start += frame_samples;
        }
        if(count < 0) {
            pw_log_warn("Opus encoder error: %s", opus_strerror(count));
        } else if(data->send != NULL) {
            data->send(opus, count, data->timestamp, data->send_closure);
        }
        data->timestamp += FRAME_SIZE_MS * 48000 / 1000;
    }

    if(start < new_samples)
        buffer(data, samples + start, new_samples - start);

    pw_stream_queue_buffer(data->stream, b);
}

static void on_stream_param_changed(void *closure,
                                    uint32_t id, const struct spa_pod *param)
{
    struct data *data = closure;
    int res;

    if(data->encoder != NULL) {
        opus_encoder_destroy(data->encoder);
        data->encoder = NULL;
    }
    data->numbuffered = 0;

    if(param == NULL || id != SPA_PARAM_Format)
        return;

    res = spa_format_parse(param,
                           &data->format.media_type,
                           &data->format.media_subtype);
    if(res < 0) {
        pw_log_error("couldn't parse data format");
        return;
    }

    if(data->format.media_type != SPA_MEDIA_TYPE_audio ||
       data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
        pw_log_error("unexpected data format");
        return;
    }

    spa_format_audio_raw_parse(param, &data->format.info.raw);

    if(data->format.info.raw.format != SPA_AUDIO_FORMAT_F32 ||
       data->format.info.raw.rate != 48000) {
        pw_log_error("unsupported data format");
        return;
    }

    data->encoder = opus_encoder_create(data->format.info.raw.rate,
                                        data->format.info.raw.channels,
                                        OPUS_APPLICATION_AUDIO,
                                        &res);
    if(res != OPUS_OK) {
        pw_log_error("Opus encoder create: %s", opus_strerror(res));
        data->encoder = NULL;
        return;
    }

    res = opus_encoder_ctl(data->encoder, OPUS_SET_INBAND_FEC(1));
    if(res != OPUS_OK)
        pw_log_warn("Couln't set Opus bitrate: %s", opus_strerror(res));

    if(data->bitrate > 0) {
        res = opus_encoder_ctl(data->encoder, OPUS_SET_BITRATE(data->bitrate));
        if(res != OPUS_OK)
            pw_log_warn("Couln't enable Opus FEC: %s", opus_strerror(res));
    }
}

static void on_destroy(void *closure) {
    struct data *data = closure;
    if(data->encoder != NULL) {
        opus_encoder_destroy(data->encoder);
        data->encoder = NULL;
    }
    data->numbuffered = 0;

}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = on_stream_param_changed,
    .process = on_process,
    .destroy = on_destroy,
};

static void do_quit(void *closure, int signal_number)
{
    struct data *data = closure;
    pw_main_loop_quit(data->loop);
}

// connect_target is either NULL (no autoconnect), the emptry string
// (autoconnect to default source), or the name of a target.
void *pw_setup(const char *connect_target, int bitrate)
{
    struct data *data = calloc(1, sizeof(struct data));
    if(data == NULL)
        return NULL;

    if(bitrate > 0)
        data->bitrate = bitrate;

    struct pw_main_loop *loop = pw_main_loop_new(NULL);
    data->loop = loop;

    pw_loop_add_signal(pw_main_loop_get_loop(data->loop), SIGINT,
                       do_quit, data);
    pw_loop_add_signal(pw_main_loop_get_loop(data->loop), SIGTERM,
                       do_quit, data);

    struct pw_properties *props;
    props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                              PW_KEY_MEDIA_CATEGORY, "Capture",
                              PW_KEY_MEDIA_ROLE, "Music",
                              NULL);
    if(connect_target != NULL && *connect_target != '\0')
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, connect_target);

    data->stream = pw_stream_new_simple(pw_main_loop_get_loop(data->loop),
                                        "whip",
                                        props,
                                        &stream_events,
                                        data);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    const struct spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(
                    &b, SPA_PARAM_EnumFormat,
                    &SPA_AUDIO_INFO_RAW_INIT(
                        .format = SPA_AUDIO_FORMAT_F32,
                        .rate = 48000));

    pw_stream_connect(data->stream,
                      PW_DIRECTION_INPUT,
                      PW_ID_ANY,
                      (connect_target != NULL ?
                       PW_STREAM_FLAG_AUTOCONNECT : 0) |
                      PW_STREAM_FLAG_MAP_BUFFERS,
                      params, 1);

    return data;
}

int pw_run(void *closure, sendfunction send, void *send_closure) {
    struct data *data = closure;

    data->send = send;
    data->send_closure = send_closure;

    return pw_main_loop_run(data->loop);
}

void pw_cleanup(void *closure) {
    struct data *data = closure;

    pw_stream_destroy(data->stream);
    pw_main_loop_destroy(data->loop);
    free(data);
}
