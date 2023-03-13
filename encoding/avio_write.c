#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>

#include <emscripten/emscripten.h>

struct buffer_data {
    uint8_t *buf;
    size_t size;
    uint8_t *ptr;
    size_t room; ///< size left in the buffer
};

AVFormatContext *ofmt_ctx = NULL;
AVIOContext *avio_ctx = NULL;
uint8_t *avio_ctx_buffer = NULL;
size_t avio_ctx_buffer_size = 4096;
int i, ret = 0;
struct buffer_data bd = { 0 };
const size_t bd_buf_size = 1024;
const char* codec_name = "libvpx";

AVFrame *video_frame;
AVStream *video_stream = NULL;
AVPacket *pkt;
AVCodecContext *video_ctx;

int frame_idx;
const int NR_COLORS = 4;
int have_video = 0;

uint8_t* yuv_buffer;
int count = 0;
char* log_str;
int should_print_log = 1;

// https://stackoverflow.com/questions/5901181/c-string-append
void _log(char* str2) {
  char * new_str ;
  if (should_print_log) {
    printf("%s", str2);
  }
  
  if((new_str = malloc(strlen(log_str)+strlen(str2)+1)) != NULL){
      new_str[0] = '\0';   // ensures the memory is an empty string
      strcat(new_str, log_str);
      strcat(new_str, str2);
      free(log_str);
      log_str = new_str;
  } else {
      printf("malloc for log string failed!\n");
      exit(1);
  }
}

void set_log(int should_log) {
  should_print_log = should_log;
}

// https://stackoverflow.com/questions/5901181/c-string-append
int _log_append(char **json, const char *format, ...)
{
    char *str = NULL;
    char *old_json = NULL, *new_json = NULL;
  
    va_list arg_ptr;
    va_start(arg_ptr, format);
    vasprintf(&str, format, arg_ptr);

    // log to console
    printf("%s", str);

    // save old json
    asprintf(&old_json, "%s", (*json == NULL ? "" : *json));

    // calloc new json memory
    new_json = (char *)calloc(strlen(old_json) + strlen(str) + 1, sizeof(char));

    strcat(new_json, old_json);
    strcat(new_json, str);

    if (*json) free(*json);
    *json = new_json;

    free(old_json);
    free(str);
  
    return 0;
}

int get_log_size() {
  return strlen(log_str);
}

char* get_log() {
  return log_str;
}

static int64_t seek (void *opaque, int64_t offset, int whence) {
    struct buffer_data *bd = (struct buffer_data *)opaque;
    switch(whence){
        case SEEK_SET:
            bd->ptr = bd->buf + offset;
            return (uintptr_t)bd->ptr;
            break;
        case SEEK_CUR:
            bd->ptr += offset;
            break;
        case SEEK_END:
            bd->ptr = (bd->buf + bd->size) + offset;
            return  (uintptr_t)bd->ptr;
            break;
        case AVSEEK_SIZE:
            return bd->size;
            break;
        default:
           return -1;
    }
    return 1;
}

static int write_packet(void *opaque, uint8_t *buf, int buf_size) {
  struct buffer_data *bd = (struct buffer_data *)opaque;
  int scaled_size =  buf_size;

  
  EM_ASM({
    Module["write_data"]($0, $1, $2)
  }, buf, scaled_size, bd->ptr - bd->buf);

  bd->ptr += scaled_size;     
  return scaled_size;
}


static void encode(AVFrame *frame, AVCodecContext* cod, AVStream* out, AVPacket* p) {    
    ret = avcodec_send_frame(cod, frame);

    if (ret < 0) {
        _log_append(&log_str, "Error: Sending a frame for encoding failed: %s\n", av_err2str(ret));
        exit(1);
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(cod, p);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            av_packet_unref(p);
            return;
        } else if (ret < 0) {
            _log_append(&log_str, "Error: Encoding failed with: %s\n", av_err2str(ret));
            exit(1);
        }

        //log_packet(ofmt_ctx, pkt, "write");
        p->stream_index = out->index;      
        av_packet_rescale_ts(p, cod->time_base, out->time_base);
        av_write_frame(ofmt_ctx, p);
        av_packet_unref(p);
    }
}

void flip_vertically(uint8_t *pixels) {
    const size_t width = video_ctx->width;
    const size_t height = video_ctx->height;
    
    const size_t stride = width * NR_COLORS;
    uint8_t *row[stride];
    uint8_t *low = pixels;
    uint8_t *high = &pixels[(height - 1) * stride];

    for (; low < high; low += stride, high -= stride) {
      memcpy(row, low, stride);
      memcpy(low, high, stride);
      memcpy(high, row, stride);
    }
}

struct fmt_conv_data {
  uint8_t *rgb;
  int start; 
  int end;
  int width;
  int height;
  int idx;
};

void rgb2yuv420p(uint8_t* rgb, int width, int height) {
    // Pad to match the linesize
    uint32_t u_start = width * height;
    
    uint32_t i    =  0; // y pos
    uint32_t upos = u_start; //image_size;
    uint32_t vpos = (u_start + u_start / 4); //upos + upos / 4;
    uint8_t r, g, b;    
    
    for (uint32_t line = 0; line < height; line++ ) {
      if (!(line % 2) ) {
          for (uint32_t x = 0; x < width; x += 2 ) {
              r = rgb[NR_COLORS * i];
              g = rgb[NR_COLORS * i + 1];
              b = rgb[NR_COLORS * i + 2];
              yuv_buffer[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;

              yuv_buffer[upos++] = ((-38*r + -74*g + 112*b) >> 8) + 128;
              yuv_buffer[vpos++] = ((112*r + -94*g + -18*b) >> 8) + 128;

              r = rgb[NR_COLORS * i];
              g = rgb[NR_COLORS * i + 1];
              b = rgb[NR_COLORS * i + 2];

              yuv_buffer[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
          }
      } else {
          for ( size_t x = 0; x < width; x += 1 ) {
              r = rgb[NR_COLORS * i];
              g = rgb[NR_COLORS * i + 1];
              b = rgb[NR_COLORS * i + 2];

              yuv_buffer[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
          }
      }
    }  
}

// Add video frame to the encoder, returns the size of the buffer
int add_video_frame (uint8_t* frame, int is_yuv) { 
  
    if (!is_yuv) {
        flip_vertically(frame);
        rgb2yuv420p(frame, video_ctx->width, video_ctx->height);
        av_image_fill_arrays (
        video_frame->data,
            video_frame->linesize, 
            yuv_buffer, 
            video_frame->format, 
            video_frame->width, 
            video_frame->height, 
            1
        ); 
    } else {
      av_image_fill_arrays (
        video_frame->data,
            video_frame->linesize, 
            frame, 
            video_frame->format, 
            video_frame->width, 
            video_frame->height, 
            1
        ); 
    }

    video_frame->pts = frame_idx++;
    encode(video_frame, video_ctx, video_stream, pkt);

    if( !(count++ % 300)) {
        _log_append(&log_str, "Frame nr: %d buf size: %d room: %d  counter: %d\n", 
        count, bd.size, 
        bd.room, 
        count
      );
    }
    
    return bd.size - bd.room;
}


void write_header() {
    _log("Writing header\n");
    ret = avformat_write_header(ofmt_ctx, NULL);

    if (ret < 0) {
        _log_append(&log_str, "Error occurred when opening output file %s\n", av_err2str(ret));
        exit(1);
    } 
    _log("Header written\n");
}

// from https://github.com/canfan/WasmVideoEncoder/commit/369ac4b09a06ec5324a78bd2f663feb73a96ff61
char *__itoa(long n) {
    int len = n==0 ? 1 : floor(log10l(labs(n)))+1;
    if (n<0) len++; // room for negative sign '-'

    char    *buf = calloc(sizeof(char), len+1); // +1 for null
    snprintf(buf, len+1, "%ld", n);
    return   buf;
}

/**
 * Initialize video settings, "index" parameters should probably be const char*'s instead 
 * w: width in pixels
 * h: height in pixels
 * fps: Frames per second
 * br: video bitrate, leave as -1 if using constant quality
 * preset_idx: see x264 presets
 * codec_idx: only libx264 is currently supported in the build though
 * format_idx: mp4 only supported by current build
 * duration: duration of video, used to estimate the total bytelength of the resulting video
 * profile_idx: index for profile
 * crf: Constant rate factor, only used if bitrate is -1 
 * */

void open_video(int w, int h, int fps, int br, int preset_idx, int codec_idx, int format_idx, int duration, int profile_idx, int crf){
    _log("Opening video\n");

    const char* codecs[] = { "libvpx", "libx264", "libopenh264" };
    const AVOutputFormat* of = av_guess_format("mp4", 0, 0);

    int padded_buf_size = 1024;
    _log_append(&log_str, "Using w: %d h: %d fps: %d br: %d preset: %d duration: %d crf %d\n", w, h, fps, br, preset_idx, duration, crf);

    // Even if we're doing the "filewriting" in JS malloc a buffer
    // just in case FFMPEG uses it for something
    bd.ptr = bd.buf = av_malloc(padded_buf_size);
    bd.size = bd.room = padded_buf_size;
    _log_append(&log_str, "padded buf size: %d\n", padded_buf_size);  

    if(!bd.buf) {
      _log("Error: Failed to create internal buffer using malloc\n");
      exit(1);
    }
    
    avio_ctx_buffer = av_malloc(avio_ctx_buffer_size);
    _log("Allocating buffer\n");
    if (!avio_ctx_buffer) {
        _log("Error: Failed to allocate memory for temporary avio buffer\n");
        exit(1);
    }

    _log("Allocating avio context\n");
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 1, &bd, NULL, &write_packet, &seek);
    if (!avio_ctx) {
        _log("Error: Failed to allocate avio_ctx\n");
        exit(1);
    }

    _log("Allocating output context \n");
    ret = avformat_alloc_output_context2(&ofmt_ctx, of, NULL, NULL);
    if (ret < 0) {
        _log_append(&log_str, "Error: Failed to create output context: %s\n", av_err2str(ret));
        exit(1);
    }
    _log_append(&log_str, "Finding encoder %s \n", codecs[2]);
    const AVCodec* video_codec = avcodec_find_encoder_by_name(codecs[2]);
    _log("Encoder received\n");
    if (!video_codec) {
        _log_append(&log_str, "Error: Codec '%s' not found\n", codec_name);
        exit(1);
    }
    
    _log("Allocating codec context\n");
    video_ctx = avcodec_alloc_context3(video_codec);
    if (!video_ctx) {
      _log("Failed to allocate video context\n");
      exit(1);
    }
    video_ctx->width = w;
    video_ctx->height = h;
    video_ctx->time_base.num = 1;
    video_ctx->time_base.den = fps;
    // video_ctx->max_b_frames = 3;

    #ifdef __EMSCRIPTEN__ 
      #ifdef __EMSCRIPTEN_PTHREADS__
        video_ctx->thread_count = emscripten_num_logical_cores();
      #else 
        video_ctx->thread_count = 1;
      #endif
      _log_append(&log_str, "Using %d threads\n", video_ctx->thread_count);
    #else
      _log("Letting x264 set thread count\n");
    #endif
      
    
    video_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    video_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (0) {
        // Can pass a string, but keeping with old implementation
        const char *presets[] = { "ultrafast", "veryfast", "fast", "medium", "slow", "veryslow" };
        const char *profiles[] = { "baseline", "main", "high" };

        ret = av_opt_set(video_ctx->priv_data, "tune", "zerolatency", 0);
        if(ret < 0) {
          _log_append(&log_str, "Could not set tune: %s\n", av_err2str(ret));
        }

        ret = av_opt_set(video_ctx->priv_data, "profile", profiles[profile_idx], 0);
        if (ret < 0) {
          _log_append(&log_str, "Could not set profile: %s\n", av_err2str(ret));
          // TODO do we need to fail (exit) here?
        }
        ret = av_opt_set(video_ctx->priv_data, "preset", presets[preset_idx], 0);
        if (ret < 0) {
          _log_append(&log_str, "Could not set preset: %s\n", av_err2str(ret));
          // TODO do we need to fail (exit) here?
        }
    } else {
      ret = av_opt_set(video_ctx->priv_data, "allow-skip-frames", "false", 0);
      if (ret < 0) {
        _log_append(&log_str, "Could not set allow-skip-frames: %s\n", av_err2str(ret));
      }

      ret = av_opt_set(video_ctx->priv_data, "slices", "1", 0);
      if (ret < 0) {
        _log_append(&log_str, "Could not set slices: %s\n", av_err2str(ret));
      }
    }

  
    // If bitrate is -1 we use constant quality instead, else set the bitrate of the context
    if (br == -1) {
      const char *crf_str = __itoa((long)crf);
      av_opt_set(video_ctx->priv_data, "crf", crf_str, AV_OPT_SEARCH_CHILDREN);
      _log_append(&log_str, "Using crf: %s %d \n", crf_str, crf);
    } else {
      video_ctx->bit_rate = br; 
    }
  
    _log_append(&log_str, "Opening codec with presets\n");
    ret = avcodec_open2(video_ctx, video_codec, NULL);
    if(ret < 0) {
        _log_append(&log_str, "Error: Failed to open codec: %s \n", av_err2str(ret));
        exit(1);
    }

    // Frame initalization
    video_frame = av_frame_alloc();
    video_frame->format = video_ctx->pix_fmt;
    video_frame->width  = w;
    video_frame->height = h;
    ret = av_frame_get_buffer(video_frame, 0);

    if(ret < 0) {
      _log_append(&log_str, "Failed to get buffer for frame %s \n", av_err2str(ret));
      exit(1);
    }

    _log("Allocating packet\n");
    pkt = av_packet_alloc();
    if(!pkt){
      _log("Error: failed to allocate packet\n");
      exit(1);
    }
    _log("Allocating stream\n");
    video_stream = avformat_new_stream(ofmt_ctx, NULL);  
    if(!video_stream){
      _log("Error: Failed createing video stream\n");
      exit(1);
    }
    
    ofmt_ctx->pb = avio_ctx;
    ofmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
    ofmt_ctx->oformat = of;

    video_stream->time_base = video_ctx->time_base;
    video_stream->id = ofmt_ctx->nb_streams - 1;
    _log("Setting parameters\n");
    ret = avcodec_parameters_from_context(video_stream->codecpar, video_ctx);
    if(ret < 0) {
       _log_append(&log_str, "Error: Failed setting parameters %s \n", av_err2str(ret));
       exit(1);
    }

    int size = (video_ctx->width * video_ctx->height * 3) / 2;
    yuv_buffer = (uint8_t*)malloc(size);

    frame_idx = 0;
    have_video = 1;
    _log("Done opening video\n");

    ret = av_frame_make_writable(video_frame);
    if (ret < 0) {
      _log_append(&log_str, "Error: Failed to make video frame writable %s \n", av_err2str(ret));
      exit(1);
    }
} 

void close_stream() {
    int ret;
    _log("Flushing video\n");
    if (have_video){
      encode(NULL, video_ctx, video_stream, pkt);
    }
    
    _log("Writing trailer\n");
    ret = av_write_trailer(ofmt_ctx);
    if (ret < 0) {
      _log_append(&log_str, "Error: writing trailer %s\n", av_err2str(ret));
      exit(1);
    }
    
    _log("Freeing context\n");
    avformat_free_context(ofmt_ctx);
    _log("Closing video\n");

    if (have_video) {
        _log("Freeing video context\n");
        avcodec_free_context(&video_ctx);    
        _log("Freeing frame\n");
        av_frame_free(&video_frame);
    }

    _log("Freeing buffers\n");
    av_freep(&avio_ctx->buffer);
    av_free(avio_ctx);
    _log("finishing close_stream\n");
    free(log_str);
}   

void free_buffer(){
    av_free(bd.buf);
}
