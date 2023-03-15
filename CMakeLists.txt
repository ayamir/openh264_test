cmake_minimum_required(VERSION 3.0.0)
project(openh264_test VERSION 0.1.0)

include(CTest)
enable_testing()

add_executable(openh264_test main.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(OPENH264_LIB_PATH "./openh264-win64/lib")
set(OPENH264_INCLUDE_PATH "./openh264-win64/include")
set(OPENH264_BIN_PATH "./openh264-win64/bin")
INCLUDE_DIRECTORIES([BEFORE] ${OPENH264_INCLUDE_PATH})

find_library(OPENH264_LIB openh264 HINTS ${OPENH264_LIB_PATH})
target_link_libraries(openh264_test ${OPENH264_LIB})

add_custom_target(copy_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENH264_BIN_PATH}/openh264-6.dll"
        $<TARGET_FILE_DIR:openh264_test>
)