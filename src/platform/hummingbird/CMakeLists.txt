set(HUMMINGBIRD_SRC
	${CMAKE_CURRENT_SOURCE_DIR}/main.c
	${CMAKE_CURRENT_SOURCE_DIR}/hb-fb.c
	${CMAKE_CURRENT_SOURCE_DIR}/hb-touch.c
	${CMAKE_CURRENT_SOURCE_DIR}/hb-ui.c)

add_executable(mgba-hummingbird ${HUMMINGBIRD_SRC})
target_compile_definitions(mgba-hummingbird PRIVATE "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}")
target_link_libraries(mgba-hummingbird ${BINARY_NAME} ${PLATFORM_LIBRARY} ${OS_LIB} ${M_LIBRARY})
debug_strip(mgba-hummingbird)
