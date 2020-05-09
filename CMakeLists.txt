cmake_minimum_required(VERSION 3.16)
project(v4l_h264_ffmpeg)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)

add_executable(main src/main.cpp)
target_link_libraries(main avcodec avdevice avformat avutil)