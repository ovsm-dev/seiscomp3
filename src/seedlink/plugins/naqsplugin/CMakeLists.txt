SET(NAQSPLUGIN_SOURCES
	naqs_plugin.c
	naqs_utils.c
	plugin.c
)

INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(naqs_plugin ${NAQSPLUGIN_SOURCES})
INSTALL(TARGETS naqs_plugin RUNTIME DESTINATION ${SEEDLINK_PLUGIN_OUTPUT_DIR})
