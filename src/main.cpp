#include <iostream>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavdevice/avdevice.h>
}

int main(int argc, char **argv)
{
  if (argc != 5) {
    std::cout << "Usage: " << argv[0] << " input fps size output" << std::endl;
    std::cout << "Example: " << argv[0] << " /dev/video2 30 640x480 camera.output" << std::endl;
    exit(1);
  }

  std::string input_fn = argv[1];
  std::string fps = argv[2];
  std::string size = argv[3];
  std::string output_fn = argv[4];

  std::cout << "Capture " << input_fn << ", " << fps << "fps, " << size << ", write to " << output_fn << std::endl;

  av_register_all();
  avdevice_register_all();
  av_log_set_level(AV_LOG_WARNING);


  // Device drivers appear as formats in ffmpeg
  // Find the v4l driver
  AVInputFormat *input_format = av_find_input_format("video4linux2");
  if (!input_format) {
    std::cerr << "Cannot find the v4l driver" << std::endl;
    exit(1);
  }

  // Allocate a format context
  // Call format_free_context() to free
  AVFormatContext *format_context = avformat_alloc_context();
  if (!format_context) {
    std::cerr << "Cannot allocate a format context" << std::endl;
    exit(1);
  }

  // Don't block
#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
  format_context->flags |= AVFMT_FLAG_NONBLOCK;
#pragma clang diagnostic pop

  // Set format options, this will allocate an AVDictionary
  AVDictionary *format_options = nullptr;
  av_dict_set(&format_options, "input_format", "h264", 0);
  av_dict_set(&format_options, "framerate", fps.c_str(), 0);
  av_dict_set(&format_options, "video_size", size.c_str(), 0);

  // Open input, pass ownership of format_options
  // Call avformat_close_input() to close
  if (avformat_open_input(&format_context, input_fn.c_str(), input_format, &format_options) < 0) {
    std::cerr << "Cannot open device " << input_fn << std::endl;
    avformat_free_context(format_context);
    exit(1);
  }

  // Get stream info from the input
  if (avformat_find_stream_info(format_context, nullptr) < 0) {
    std::cerr << "Cannot find stream info" << std::endl;
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  // Dump the stream info to stderr
  // av_dump_format(format_context, 0, input_fn.c_str(), 0);

  // Find the video stream (vs audio, metadata, etc.)
  int stream_idx = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_idx < 0) {
    std::cerr << "Cannot find video stream" << std::endl;
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  AVStream *stream = format_context->streams[stream_idx];

  // TODO deprecated, switch to AVCodecParameters *codecpar
  // TODO this means we don't have a context, so need to call avcodec_alloc_context3()
  AVCodecContext *codec_context = stream->codec;

  // Find the appropriate decoder
  // TODO always get the H264 decoder -- move this up and change the cleanup order
  AVCodec *decoder = avcodec_find_decoder(codec_context->codec_id);
  if (!decoder) {
    std::cerr << "Cannot find codec" << std::endl;
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  std::cout << "To play the video:" << std::endl
            << "ffplay -f rawvideo -pixel_format " << av_get_pix_fmt_name(codec_context->pix_fmt)
            << " -video_size " << codec_context->width << "x" << codec_context->height << " " << output_fn << std::endl;

  // Set decoder options, this will allocate an AVDictionary
  // TODO is refcounted_frames required? seems to change who owns what
  AVDictionary *decoder_options = nullptr;
  av_dict_set(&decoder_options, "refcounted_frames", "1", 0);

  // Open the decoder, pass ownership of decoder_options
  if (avcodec_open2(codec_context, decoder, &decoder_options) < 0) {
    std::cerr << "Cannot open codec" << std::endl;
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  // Allocate image
  // Call av_freep() to free
  uint8_t *image_pointers[4] = {nullptr};
  int image_linesizes[4];
  int image_size = av_image_alloc(image_pointers, image_linesizes,
                                  codec_context->width, codec_context->height,
                                  codec_context->pix_fmt, 1);
  if (image_size < 0) {
    std::cerr << "Cannot allocate image" << std::endl;
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  // Allocate frame and set to default values
  // Call av_frame_free() to free
  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    std::cerr << "Cannot allocate frame" << std::endl;
    av_freep(image_pointers);
    avcodec_close(codec_context);
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  // Open destination file for writing
  FILE *output_file = fopen(output_fn.c_str(), "wb");
  if (!output_file) {
    std::cerr << "Cannot open output file" << std::endl;
    av_frame_free(&frame);
    av_freep(image_pointers);
    avcodec_close(codec_context);
    avformat_close_input(&format_context);
    avformat_free_context(format_context);
    exit(1);
  }

  // Process frames
  // int num_frames = 0;
  while (true) {
    AVPacket packet;

    // Read one frame
    // Call av_packet_unref() to free packet
    int rc = av_read_frame(format_context, &packet);
    if (rc < 0) {
      if (rc == AVERROR(EAGAIN)) {
        // TODO TODO TODO this spins until a frame is ready! Need a blocking routine, or a callback mechanism
        continue;
      } else {
        break;
      }
    }

    // Decode frame
    // TODO deprecated, use avcodec_send_packet() and avcodec_receive_frame()
    int got_frame;
    if (avcodec_decode_video2(codec_context, frame, &got_frame, &packet) < 0 || !got_frame) {
      std::cerr << "Cannot decode frame" << std::endl;
      av_packet_unref(&packet);
      continue;
    }

    // std::cout << "Decoded frame " << num_frames++ << " (" << frame->coded_picture_number << ")" << std::endl;

    // Write decoded frame into image
    av_image_copy(image_pointers, image_linesizes,
                  (const uint8_t **) (frame->data), frame->linesize,
                  codec_context->pix_fmt, codec_context->width, codec_context->height);

    // Write to file
    fwrite(image_pointers[0], 1, image_size, output_file);

    // Free packet
    av_packet_unref(&packet);
  }

  // Will never get here....
  av_frame_free(&frame);
  av_freep(image_pointers);
  avcodec_close(codec_context);
  avformat_close_input(&format_context);
  avformat_free_context(format_context);
  exit(0);
}

