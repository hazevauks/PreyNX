# Homebrew cannot load a separate game library, but the game code is written for
# DLL semantics: it keeps its own copies of the engine interface globals and its
# own idlib, and exposes only GetGameAPI. To keep that behavior:
#
#   1. its objects are linked relocatably into one object, dissolving COMDAT
#      groups so the game's copies of inline functions and vtables can never
#      replace the engine's at the final link;
#   2. every defined symbol except GetGameAPI is made local.
#
# With LTO the first step also runs the optimizer: objcopy cannot localize the
# symbols of LTO bytecode.
function(prey_add_game_object name objects_target)
	set(output ${CMAKE_BINARY_DIR}/${name}.o)
	set(partial ${CMAKE_BINARY_DIR}/${name}.partial.o)
	set(rsp ${CMAKE_BINARY_DIR}/${name}.rsp)

	set(lto_flags "")
	if(PREY_LTO)
		set(lto_flags -flto=auto -flinker-output=nolto-rel)
	endif()

	# relative paths: the compiler is a native program on Windows, and does not
	# understand the paths CMake writes under MSYS2
	file(GENERATE OUTPUT ${rsp} CONTENT "$<JOIN:$<FILTER:$<PATH:RELATIVE_PATH,$<TARGET_OBJECTS:${objects_target}>,${CMAKE_BINARY_DIR}>,EXCLUDE,[.]gch$>,\n>\n")

	add_custom_command(
		OUTPUT ${output}
		COMMAND ${CMAKE_CXX_COMPILER} -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft
			-r -nostdlib -Wl,--force-group-allocation ${lto_flags} @${name}.rsp -o ${name}.partial.o
		COMMAND ${CMAKE_OBJCOPY} --keep-global-symbol=GetGameAPI ${name}.partial.o ${name}.o
		COMMAND ${CMAKE_COMMAND} -E remove ${partial}
		DEPENDS ${objects_target} $<TARGET_OBJECTS:${objects_target}> ${rsp}
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "Linking the game module object"
		VERBATIM
	)
	add_custom_target(${name}_target DEPENDS ${output})
	set_source_files_properties(${output} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
	set(PREY_GAME_OBJECT ${output} PARENT_SCOPE)
endfunction()
