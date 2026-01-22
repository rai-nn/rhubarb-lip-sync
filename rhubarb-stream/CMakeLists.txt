cmake_minimum_required(VERSION 3.24)

project(rhubarb-stream VERSION 1.0.0 LANGUAGES C CXX)

# Support legacy OS X versions
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable POSIX threads
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

# Set global flags
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(enableWarningsFlags "-Wall;-Wextra")
	set(disableWarningsFlags "-w")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	set(enableWarningsFlags "/W4")
	set(disableWarningsFlags "/W0")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4458 /utf-8 /D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
endif()

if(${UNIX})
	add_definitions(-DHAVE_UNISTD_H)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# =============================================================================
# Shared lib/ from rhubarb (sibling directory)
# =============================================================================
set(RHUBARB_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../rhubarb/lib")

# Validate that rhubarb lib directory exists
if(NOT EXISTS "${RHUBARB_LIB}")
	message(FATAL_ERROR "Rhubarb lib directory not found at ${RHUBARB_LIB}. "
		"Ensure rhubarb-lip-sync submodule is properly initialized.")
endif()

# ... Boost
cmake_policy(SET CMP0167 OLD)
find_package(Boost 1.54 REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
link_libraries(Boost::headers)

# ... C++ Format
FILE(GLOB cppFormatFiles "${RHUBARB_LIB}/cppformat/*.cc")
add_library(cppFormat ${cppFormatFiles})
target_include_directories(cppFormat SYSTEM PUBLIC "${RHUBARB_LIB}/cppformat")
target_compile_options(cppFormat PRIVATE ${disableWarningsFlags})
set_target_properties(cppFormat PROPERTIES FOLDER lib)

# ... sphinxbase
FILE(GLOB_RECURSE sphinxbaseFiles "${RHUBARB_LIB}/sphinxbase-rev13216/src/libsphinxbase/*.c")
add_library(sphinxbase ${sphinxbaseFiles})
target_include_directories(sphinxbase SYSTEM PUBLIC
	"${RHUBARB_LIB}/sphinxbase-rev13216/include"
	"${RHUBARB_LIB}/sphinxbase-rev13216/src"
	"${RHUBARB_LIB}/sphinx_config"
)
target_compile_options(sphinxbase PRIVATE ${disableWarningsFlags})
target_compile_definitions(sphinxbase PUBLIC __SPHINXBASE_EXPORT_H__=1 SPHINXBASE_EXPORT=)
set_target_properties(sphinxbase PROPERTIES FOLDER lib)

# ... PocketSphinx
FILE(GLOB pocketSphinxFiles "${RHUBARB_LIB}/pocketsphinx-rev13216/src/libpocketsphinx/*.c")
add_library(pocketSphinx ${pocketSphinxFiles})
target_include_directories(pocketSphinx SYSTEM PUBLIC
	"${RHUBARB_LIB}/pocketsphinx-rev13216/include"
	"${RHUBARB_LIB}/pocketsphinx-rev13216/src/libpocketsphinx"
)
target_link_libraries(pocketSphinx sphinxbase)
target_compile_options(pocketSphinx PRIVATE ${disableWarningsFlags})
target_compile_definitions(pocketSphinx PUBLIC __POCKETSPHINX_EXPORT_H__=1 POCKETSPHINX_EXPORT=)
set_target_properties(pocketSphinx PROPERTIES FOLDER lib)

# ... WebRTC
set(webRtcFiles
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/cross_correlation.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/division_operations.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/downsample_fast.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/energy.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/get_scaling_square.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/min_max_operations.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/resample_48khz.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/resample_by_2_internal.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/resample_fractional.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/spl_init.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/spl_inl.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/signal_processing/vector_scaling_operations.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/vad/vad_core.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/vad/vad_filterbank.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/vad/vad_gmm.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/vad/vad_sp.c
	${RHUBARB_LIB}/webrtc-8d2248ff/webrtc/common_audio/vad/webrtc_vad.c
)
add_library(webRtc ${webRtcFiles})
target_include_directories(webRtc SYSTEM PUBLIC "${RHUBARB_LIB}/webrtc-8d2248ff")
target_compile_options(webRtc PRIVATE ${disableWarningsFlags})
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(webRtc PRIVATE -pthread -lpthread)
endif()
if (NOT WIN32)
	target_compile_definitions(webRtc PRIVATE WEBRTC_POSIX)
endif()
set_target_properties(webRtc PROPERTIES FOLDER lib)

# ... GSL
include_directories(SYSTEM "${RHUBARB_LIB}/gsl/include")

# ... nlohmann/json (single-header JSON library)
include_directories(SYSTEM "${RHUBARB_LIB}/nlohmann-json")

# =============================================================================
# Rhubarb Stream Sources
# =============================================================================

include_directories("src")

set(CORE_SOURCES
	src/core/Phone.cpp
	src/core/Phone.h
	src/core/Shape.cpp
	src/core/Shape.h
)

set(TIME_SOURCES
	src/time/centiseconds.cpp
	src/time/centiseconds.h
	src/time/TimeRange.cpp
	src/time/TimeRange.h
	src/time/Timed.h
	src/time/Timeline.h
	src/time/BoundedTimeline.h
	src/time/ContinuousTimeline.h
)

set(ANIMATION_SOURCES
	src/animation/animationRules.cpp
	src/animation/animationRules.h
	src/animation/ShapeRule.cpp
	src/animation/ShapeRule.h
	src/animation/shapeShorthands.h
)

set(CONFIG_SOURCES
	src/config/PauseDetectionConfig.h
)

set(TOOLS_SOURCES
	src/tools/EnumConverter.h
	src/tools/array.h
	src/tools/parallel.h
	src/tools/progress.cpp
	src/tools/progress.h
	src/tools/tools.h
)

set(PROTOCOL_SOURCES
	src/protocol/FrameTypes.h
	src/protocol/FrameReader.cpp
	src/protocol/FrameReader.h
	src/protocol/FrameWriter.cpp
	src/protocol/FrameWriter.h
)

set(QUEUE_SOURCES
	src/queue/Sentence.h
	src/queue/SentenceQueue.h
	src/queue/SentenceQueue.cpp
	src/queue/WorkerPool.h
	src/queue/WorkerPool.cpp
)

# =============================================================================
# Static Library
# =============================================================================

add_library(rhubarb-stream-core STATIC
	${CORE_SOURCES}
	${TIME_SOURCES}
	${ANIMATION_SOURCES}
	${CONFIG_SOURCES}
	${TOOLS_SOURCES}
	${PROTOCOL_SOURCES}
	${QUEUE_SOURCES}
)

target_link_libraries(rhubarb-stream-core
	cppFormat
	pocketSphinx
	webRtc
)

target_compile_options(rhubarb-stream-core PRIVATE ${enableWarningsFlags})
set_target_properties(rhubarb-stream-core PROPERTIES FOLDER rhubarb-stream)

# =============================================================================
# Executable
# =============================================================================

add_executable(rhubarb-stream
	src/main.cpp
)

target_link_libraries(rhubarb-stream
	rhubarb-stream-core
)

target_compile_options(rhubarb-stream PRIVATE ${enableWarningsFlags})
set_target_properties(rhubarb-stream PROPERTIES FOLDER rhubarb-stream)

# =============================================================================
# Resources: Copy sphinx models
# =============================================================================

function(copy_and_install sourceGlob relativeTargetDirectory)
	file(GLOB_RECURSE files ${sourceGlob})
	foreach(file ${files})
		file(RELATIVE_PATH relativePath ${RHUBARB_LIB} ${file})
		get_filename_component(relativeDir ${relativePath} DIRECTORY)
		file(COPY ${file} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/${relativeTargetDirectory}/${relativeDir})
	endforeach()
endfunction()

copy_and_install("${RHUBARB_LIB}/pocketsphinx-rev13216/model/en-us/*" "res/sphinx")
copy_and_install("${RHUBARB_LIB}/cmusphinx-en-us-5.2/*" "res/sphinx/acoustic-model")

# =============================================================================
# Installation
# =============================================================================

install(TARGETS rhubarb-stream DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/res DESTINATION .)
