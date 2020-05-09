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

  // Find the h264 decoder
  AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    std::cerr << "Could not find the h264 codec" << std::endl;
    exit(1);
  }

  // Allocate a codec context
  // Free: avcodec_free_context()
  AVCodecContext *codec_context = avcodec_alloc_context3(codec);
  if (!codec_context) {
    std::cerr << "Could not allocate a codec context" << std::endl;
    exit(1);
  }

  // Device drivers appear as formats in ffmpeg
  // Find the v4l driver
  AVInputFormat *input_format = av_find_input_format("video4linux2");
  if (!input_format) {
    std::cerr << "Could not find the v4l driver" << std::endl;
    exit(1);
  }

  // Allocate a format context
  // Free: format_free_context()
  AVFormatContext *format_context = avformat_alloc_context();
  if (!format_context) {
    std::cerr << "Could not allocate a format context" << std::endl;
    exit(1);
  }

  // Set format options, this will allocate an AVDictionary
  AVDictionary *format_options = nullptr;
  av_dict_set(&format_options, "input_format", "h264", 0);
  av_dict_set(&format_options, "framerate", fps.c_str(), 0);
  av_dict_set(&format_options, "video_size", size.c_str(), 0);

  // Open input, pass ownership of format_options
  if (avformat_open_input(&format_context, input_fn.c_str(), input_format, &format_options) < 0) {
    std::cerr << "Could not open the v4l device " << input_fn << std::endl;
    exit(1);
  }

  // Get stream info from the input
  if (avformat_find_stream_info(format_context, nullptr) < 0) {
    std::cerr << "Could not find the device stream info" << std::endl;
    exit(1);
  }

  // Dump the stream info to stderr
  // av_dump_format(format_context, 0, input_fn.c_str(), 0);

  // Find the video stream (vs audio, metadata, etc.)
  int stream_idx = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_idx < 0) {
    std::cerr << "Could not find a video stream on the device" << std::endl;
    exit(1);
  }

  AVStream *stream = format_context->streams[stream_idx];

  // Get the codec parameters
  // We'll look at width, height, [pixel] format
  AVCodecParameters *codec_params = stream->codecpar;

  std::cout << "To play the video:" << std::endl << "ffplay -f rawvideo -pixel_format "
            << av_get_pix_fmt_name(static_cast<AVPixelFormat>(codec_params->format))
            << " -video_size " << codec_params->width << "x" << codec_params->height << " " << output_fn << std::endl;

  // Open the decoder, pass ownership of decoder_options
  if (avcodec_open2(codec_context, codec, nullptr) < 0) {
    std::cerr << "Could not open codec" << std::endl;
    exit(1);
  }

  // Allocate image
  // Free: av_freep()
  uint8_t *image_pointers[4] = {nullptr};
  int image_linesizes[4];
  int image_size = av_image_alloc(image_pointers, image_linesizes,
                                  codec_params->width, codec_params->height,
                                  static_cast<AVPixelFormat>(codec_params->format), 1);
  if (image_size < 0) {
    std::cerr << "Could not allocate image" << std::endl;
    exit(1);
  }

  // Allocate frame and set to default values
  // Free: av_frame_free()
  AVFrame *frame = av_frame_alloc();
  if (!frame) {
    std::cerr << "Could not allocate frame" << std::endl;
    exit(1);
  }

  // Open destination file for writing
  FILE *output_file = fopen(output_fn.c_str(), "wb");
  if (!output_file) {
    std::cerr << "Could not open output file" << std::endl;
    exit(1);
  }

  // Process frames from the v4l device
  // int num_frames = 0;
  while (true) {
    AVPacket packet;

    // Read one frame from v4l
    // Free: av_packet_unref
    if (av_read_frame(format_context, &packet) < 0) {
      std::cout << "EOF" << std::endl;
      break;
    }

    // Send packet to decoder
    if (avcodec_send_packet(codec_context, &packet) < 0) {
      std::cerr << "Could not send packet" << std::endl;
      av_packet_unref(&packet);
      continue;
    }

    // Get decoded frame
    if (avcodec_receive_frame(codec_context, frame) < 0) {
      std::cerr << "Could not receive frame" << std::endl;
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

  fclose(output_file);
  exit(0);
}

