cmake_minimum_required(VERSION 3.10)

project(md5-gif-killer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "-w -O3")

find_package(Boost REQUIRED)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})

add_executable(gif-gen gif-gen.cpp)

find_program(CLANG_FORMAT "clang-format")
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
        ${PROJECT_SOURCE_DIR}/*.[ch]pp
        ${PROJECT_SOURCE_DIR}/*.[ch]
    )

    add_custom_target(
        clang-force-format
        COMMAND clang-format
        -i
        -style=file
        ${ALL_CXX_SOURCE_FILES}
    )
endif()