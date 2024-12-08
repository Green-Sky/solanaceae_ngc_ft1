cmake_minimum_required(VERSION 3.9...3.24 FATAL_ERROR)

########################################

add_library(plugin_ngcft1 MODULE
	./plugin_ngcft1.cpp
)
target_compile_features(plugin_ngcft1 PUBLIC cxx_std_17)
set_target_properties(plugin_ngcft1 PROPERTIES
	C_VISIBILITY_PRESET hidden
)
target_compile_definitions(plugin_ngcft1 PUBLIC ENTT_API_IMPORT)
target_link_libraries(plugin_ngcft1 PUBLIC
	solanaceae_plugin
	solanaceae_ngcext
	solanaceae_ngcft1
	solanaceae_sha1_ngcft1
)

########################################

add_library(plugin_ngchs2 MODULE
	./plugin_ngchs2.cpp
)
target_compile_features(plugin_ngchs2 PUBLIC cxx_std_17)
set_target_properties(plugin_ngchs2 PROPERTIES
	C_VISIBILITY_PRESET hidden
)
target_compile_definitions(plugin_ngchs2 PUBLIC ENTT_API_IMPORT)
target_link_libraries(plugin_ngchs2 PUBLIC
	solanaceae_plugin
	solanaceae_ngchs2
)
