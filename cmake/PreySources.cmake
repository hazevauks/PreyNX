file(GLOB PREY_IDLIB_SOURCES CONFIGURE_DEPENDS
	${SRC}/idlib/*.cpp
	${SRC}/idlib/bv/*.cpp
	${SRC}/idlib/containers/*.cpp
	${SRC}/idlib/geometry/*.cpp
	${SRC}/idlib/hashing/*.cpp
	${SRC}/idlib/math/*.cpp
)
list(REMOVE_ITEM PREY_IDLIB_SOURCES ${SRC}/idlib/math/Simd_AltiVec.cpp)

file(GLOB PREY_ENGINE_SOURCES CONFIGURE_DEPENDS
	${SRC}/aas/*.cpp
	${SRC}/cm/*.cpp
	${SRC}/framework/*.cpp
	${SRC}/framework/async/*.cpp
	${SRC}/renderer/*.cpp
	${SRC}/renderer/Color/*.cpp
	${SRC}/renderer/DXT/*.cpp
	${SRC}/renderer/deko3d/*.cpp
	${SRC}/renderer/jpeg-6/*.c
	${SRC}/sound/*.cpp
	${SRC}/sound/OpenAL/*.cpp
	${SRC}/sys/*.cpp
	${SRC}/sys/switch/*.cpp
	${SRC}/ui/*.cpp
)
list(APPEND PREY_ENGINE_SOURCES ${SRC}/external/stb_vorbis/stb_vorbis.c)

file(GLOB PREY_NO_PCH_SOURCES CONFIGURE_DEPENDS
	${SRC}/renderer/jpeg-6/*.c
	${SRC}/sys/switch/switch_net.cpp
	${SRC}/sys/switch/switch_audio.cpp
)
list(APPEND PREY_NO_PCH_SOURCES ${SRC}/external/stb_vorbis/stb_vorbis.c)

file(GLOB_RECURSE PREY_GAME_SOURCES CONFIGURE_DEPENDS
	${SRC}/game/*.cpp
	${SRC}/Prey/*.cpp
)
list(REMOVE_ITEM PREY_GAME_SOURCES
	${SRC}/game/Callbacks.cpp
	${SRC}/game/gamesys/Callbacks.cpp
	${SRC}/Prey/ai_Navigator.cpp
	${SRC}/Prey/game_events.cpp
	${SRC}/Prey/particles_particles.cpp
	${SRC}/Prey/prey_weapon.cpp
)
