cmake_minimum_required(VERSION 3.0)

project(iceFUNprog2 VERSION "0.1.0" LANGUAGES CXX)
add_definitions(-DVERSION=\"v${PROJECT_VERSION}\")

option(BUILD_STATIC "Whether or not to build with static libraries" OFF)
option(USE_PKGCONFIG "Use pkgconfig to find libraries" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -Wextra ${CMAKE_CXX_FLAGS_DEBUG}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

if (BUILD_STATIC)
	set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static ${CMAKE_EXE_LINKER_FLAGS}")
	set(BUILD_SHARED_LIBS OFF)
endif()

if (USE_PKGCONFIG)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
endif()

set(SOURCE
	src/iceFUNprog2.cpp
)

set(HEADERS
	src/cdcacm.hpp
	src/cmdline.hpp
)

link_directories(
	${LIBUSB_LIBRARY_DIRS}
)

add_executable(iceFUNprog2
	${SOURCE}
	${HEADERS}
)

include_directories(
	${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(iceFUNprog2
	${LIBUSB_LIBRARIES}
)

if (BUILD_STATIC)
	set_target_properties(iceFUNprog2 PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()

install(TARGETS iceFUNprog2 DESTINATION /usr/local/bin)
