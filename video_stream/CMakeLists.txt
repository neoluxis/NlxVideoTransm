cmake_minimum_required(VERSION 3.10)

project(VideoStream)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 OpenCV
find_package(OpenCV REQUIRED)

if(OpenCV_FOUND)
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
else()
    message(FATAL_ERROR "OpenCV not found")
endif()

# 设置 OpenCV 头文件路径
include_directories(${OpenCV_INCLUDE_DIRS})

# 设置源文件
set(SOURCE_FILES
    server.cpp
)

add_executable(
    server
    ${SOURCE_FILES}
)

# 链接 OpenCV 和 pthread
target_link_libraries(server
    ${OpenCV_LIBS}
    pthread
)

# 设置安装
install(TARGETS server RUNTIME DESTINATION /usr/local/bin RENAME vstream)
install(FILES video-stream.service DESTINATION /usr/lib/systemd/system)
install(CODE "execute_process(COMMAND systemctl daemon-reload)")

