cmake_minimum_required(VERSION 3.16)
project(v4l_h264_ffmpeg)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)

add_executable(v4l_to_file src/main.cpp)
target_link_libraries(v4l_to_file avcodec avdevice avformat avutil)