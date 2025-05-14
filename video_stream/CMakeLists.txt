cmake_minimum_required(VERSION 3.10)

project(VideoStream)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

if(OpenCV_FOUND)
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
else()
    message(FATAL_ERROR "OpenCV not found")
endif()

include_directories(
    src
    src/include
)

# 主程序源文件
file(GLOB_RECURSE SOURCE_FILES "src/*.cpp")
# neolux 库源文件
file(GLOB_RECURSE NXLIB_SOURCE_FILES "src/neolux/*.cpp")

add_library(nxlib STATIC ${NXLIB_SOURCE_FILES})
# add_library(libneolux SHARED ${NXLIB_SOURCE_FILES})

list(REMOVE_ITEM SOURCE_FILES ${NXLIB_SOURCE_FILES})

add_executable(server ${SOURCE_FILES})

target_link_libraries(server
    nxlib
    ${OpenCV_LIBS}
    pthread
)

install(TARGETS server RUNTIME DESTINATION /usr/local/bin RENAME vstream)
install(FILES video-stream.service DESTINATION /usr/lib/systemd/system)
install(CODE "execute_process(COMMAND systemctl daemon-reload)")

