function(blit_executable NAME SOURCES)
	add_executable(${NAME} ${SOURCES} ${ARGN})

	install(TARGETS ${NAME}
		RUNTIME DESTINATION bin
	)

	if (${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC)
		target_link_libraries(${NAME} BlitHalSDL)
	elseif(${CMAKE_SYSTEM_NAME} STREQUAL Darwin)
  		target_link_libraries(${NAME} -Wl,-force_load BlitHalSDL)
	else()
  		target_link_libraries(${NAME} -Wl,--whole-archive BlitHalSDL -Wl,--no-whole-archive)
	endif()

	if(EMSCRIPTEN)
		set_target_properties(${NAME} PROPERTIES
			SUFFIX ".html"
			LINK_FLAGS "-s ENVIRONMENT=web -s SDL2_IMAGE_FORMATS=['jpg']"
		)
	endif()
endfunction()

function(blit_executable_int_flash NAME SOURCES)
	blit_executable(${NAME} ${SOURCES} ${ARGN})
endfunction()

function(blit_metadata TARGET FILE)
	# do nothing
endfunction()