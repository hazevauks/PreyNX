cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{DEVKITPRO})
	set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

project(PreyNX VERSION 0.1.0 LANGUAGES C CXX ASM)

if(NOT NINTENDO_SWITCH)
	message(FATAL_ERROR "PreyNX targets the Nintendo Switch: configure it with the devkitA64 toolchain.")
endif()

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

option(PREY_LTO "Link-time optimization (slower links, faster game)" OFF)

set(SRC ${CMAKE_SOURCE_DIR}/src)

add_compile_definitions(
	USE_OPENAL
	HUMANHEAD=1
	PREY_VERSION="${PROJECT_VERSION}"
)
# id's code leans on type punning, and frame pointers keep the crash reports useful
add_compile_options(
	-fno-strict-aliasing
	-fno-omit-frame-pointer
	-w
	"$<$<COMPILE_LANGUAGE:CXX>:-fpermissive>"
	"$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>"
	"$<$<COMPILE_LANGUAGE:CXX>:-frtti>"
)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if(PREY_LTO)
	add_compile_options(-flto=auto -fno-fat-lto-objects)
	add_link_options(-flto=auto)
endif()

include(cmake/PreySources.cmake)
include(cmake/PreyGameObject.cmake)
include(cmake/PreyShaders.cmake)

# ---------------------------------------------------------------------------
# Game module: its own copy of idlib and of the engine interface globals, as on
# PC, where it is a separate library. See cmake/PreyGameObject.cmake.
# ---------------------------------------------------------------------------
add_library(prey_game_code OBJECT ${PREY_GAME_SOURCES} ${PREY_IDLIB_SOURCES})
target_include_directories(prey_game_code PRIVATE ${CMAKE_SOURCE_DIR} ${SRC})
target_compile_definitions(prey_game_code PRIVATE GAME_DLL)
target_precompile_headers(prey_game_code PRIVATE <idlib/precompiled.h>)
prey_add_game_object(prey_game_object prey_game_code)

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
add_executable(prey ${PREY_ENGINE_SOURCES} ${PREY_IDLIB_SOURCES} ${PREY_GAME_OBJECT})
target_include_directories(prey PRIVATE ${CMAKE_SOURCE_DIR} ${SRC} ${SRC}/external/stb_vorbis ${DEVKITPRO}/portlibs/switch/include)
target_compile_definitions(prey PRIVATE __DOOM_DLL__)
target_precompile_headers(prey PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:<idlib/precompiled.h>>")
set_source_files_properties(${PREY_NO_PCH_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_link_options(prey PRIVATE
	-Wl,--wrap=pthread_create
	-Wl,-Map=${CMAKE_BINARY_DIR}/prey.map
	-Wl,--gc-sections
)
target_link_libraries(prey PRIVATE deko3d openal z nx m)
add_dependencies(prey prey_game_object_target prey_shaders)

nx_generate_nacp(prey.nacp
	NAME "Prey"
	AUTHOR "hazevauks"
	VERSION "${PROJECT_VERSION}"
)
nx_create_nro(prey
	NACP prey.nacp
	ICON ${CMAKE_SOURCE_DIR}/assets/icon.jpg
	ROMFS prey_romfs
)
