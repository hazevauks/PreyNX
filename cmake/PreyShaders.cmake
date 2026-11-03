# The renderer's own shaders: GLSL compiled with uam into the NRO's romfs.
set(PREY_ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)
file(MAKE_DIRECTORY ${PREY_ROMFS_DIR}/shaders)

file(GLOB PREY_SHADER_SOURCES CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/shaders/*.vert
	${CMAKE_SOURCE_DIR}/shaders/*.frag
)

set(PREY_SHADER_OUTPUTS "")
foreach(source IN LISTS PREY_SHADER_SOURCES)
	get_filename_component(name ${source} NAME)
	get_filename_component(stage ${source} LAST_EXT)
	string(SUBSTRING ${stage} 1 -1 stage)
	set(output ${PREY_ROMFS_DIR}/shaders/${name}.dksh)
	add_custom_command(
		OUTPUT ${output}
		COMMAND ${NX_UAM_EXE} -s ${stage} -o ${output} ${source}
		DEPENDS ${source}
		COMMENT "Compiling shader ${name}"
		VERBATIM
	)
	list(APPEND PREY_SHADER_OUTPUTS ${output})
endforeach()

add_custom_target(prey_shaders DEPENDS ${PREY_SHADER_OUTPUTS})

dkp_add_asset_target(prey_romfs ${PREY_ROMFS_DIR})
add_dependencies(prey_romfs prey_shaders)
set_property(TARGET prey_romfs APPEND PROPERTY DKP_ASSET_FILES ${PREY_SHADER_OUTPUTS})
