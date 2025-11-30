cmake_minimum_required(VERSION 3.16)

# Avoid using git to determine version when building outside a git repo
set(PROJECT_VER "1")

# In CI/archives without VCS metadata, tell ESP-IDF to skip all git queries
set(IDF_SKIP_GIT ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32-s3-slcan VERSION ${PROJECT_VER})
