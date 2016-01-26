cmake_minimum_required(VERSION 2.6)
project(wvstream)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")
SET(CMAKE_CXX_COMPILER g++-4.9)


add_definitions(
        -DNOKEYATEND
        -DNEWALLOTMENTS
)

include_directories (
        ../
        ../libbento4/Core/
)

add_executable (wvstream
        wvstream.cpp
		oscompat.cpp
        dash/DASHTree.cpp
        dash/DASHStream.cpp
        helpers.cpp
        cdm/base/native_library.cc
        cdm/base/native_library_win.cc
        cdm/media/cdm/cdm_adapter.cc
)

target_link_libraries (wvstream
        curl
        expat
        bento4
)
