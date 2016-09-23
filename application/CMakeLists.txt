cmake_minimum_required(VERSION 2.6)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # require at least gcc 4.8
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must be at least 4.8!")
    endif()
endif()

project(wvstream)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")

add_definitions(
	-DNOKEYATEND
	-DNEWALLOTMENTS
)

include_directories (
	../
	../libbento4/Core/
)

subdirs (
	../libbento4
)

add_executable (wvstream
	wvstream.cpp
	oscompat.cpp
	dash/DASHTree.cpp
	dash/DASHStream.cpp
	helpers.cpp
	jsmn.c
	cdm/base/native_library.cc
	cdm/base/native_library_posix.cc
	cdm/media/cdm/cdm_adapter.cc
)

target_link_libraries (wvstream
	curl
	expat
	bento4
	dl
	pthread
)
