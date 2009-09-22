# Find, include and link OpenAL Find is already called in the top-level 
# CMakeLists

macro (FIND_OPENAL)
	if (NOT MSVC)
		find_package (PkgConfig)
		pkg_search_module (openal openal)
		if (NOT openal_INCLUDE_DIRS AND NOT openal_LIBRARIES)
			message (FATAL_ERROR "OpenAL not found by pkg-config")
		endif (NOT openal_INCLUDE_DIRS AND NOT openal_LIBRARIES)
		separate_arguments (openal_INCLUDE_DIRS)
		separate_arguments (openal_LIBRARIES)
	endif (NOT MSVC)
endmacro (FIND_OPENAL)

macro (INCLUDE_OPENAL)
	if (MSVC)
		include_directories (${ENV_NAALI_DEP_PATH}/OpenAL/include)
		link_directories (${ENV_NAALI_DEP_PATH}/OpenAL/libs/Win32)
	else (MSVC)
		include_directories (${openal_INCLUDE_DIRS} 
            /usr/include/AL /usr/local/include/AL) #HACK: pkg-config expects you to include <AL/al.h>, so returns null for INCLUDE_DIRS
		link_directories (${openal_LIBDIR})
	endif (MSVC)
endmacro (INCLUDE_OPENAL)

macro (LINK_OPENAL)
	if (MSVC)
		target_link_libraries (${TARGET_NAME}
			debug OpenAL32
			optimized OpenAL32)
	else (MSVC)
		target_link_libraries (${TARGET_NAME} ${openal_LIBRARIES})
	endif (MSVC)
endmacro (LINK_OPENAL)
