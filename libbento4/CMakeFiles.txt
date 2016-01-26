cmake_minimum_required(VERSION 2.6)
project(libbento4)

include_directories (
	Core/
	Crypto/
	MetaData/
	Codecs/
)

FILE(GLOB SOURCEFILES 
	Codecs/*.cpp 
	Core/*.cpp 
	System/*.cpp 
	System/Posix/*.cpp 
	Crypto/*.cpp 
	System/StdC/*.cpp 
	MetaData/*.cpp
)

add_library (bento4 STATIC ${SOURCEFILES})
