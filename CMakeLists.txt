cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

add_subdirectory(./external)

project(solanaceae)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(SOLANACEAE_NGCFT1_STANDALONE ON)
else()
	set(SOLANACEAE_NGCFT1_STANDALONE OFF)
endif()
message("II SOLANACEAE_NGCFT1_STANDALONE " ${SOLANACEAE_NGCFT1_STANDALONE})

option(SOLANACEAE_NGCFT1_BUILD_PLUGINS "Build the solanaceae_ngcft1 plugins" ${SOLANACEAE_NGCFT1_BUILD_PLUGINS})

# TODO: move this stuff to src
########################################

add_library(solanaceae_ngcext
	./solanaceae/ngc_ext/ngcext.hpp
	./solanaceae/ngc_ext/ngcext.cpp
)
target_include_directories(solanaceae_ngcext PUBLIC .)
target_compile_features(solanaceae_ngcext PUBLIC cxx_std_17)
target_link_libraries(solanaceae_ngcext PUBLIC
	solanaceae_toxcore
	solanaceae_util
)

########################################

add_library(solanaceae_ngcft1
	./solanaceae/ngc_ft1/ngcft1_file_kind.hpp
	./solanaceae/ngc_ft1/ngcft1.hpp
	./solanaceae/ngc_ft1/ngcft1.cpp

	./solanaceae/ngc_ft1/cca.hpp
	./solanaceae/ngc_ft1/flow_only.hpp
	./solanaceae/ngc_ft1/flow_only.cpp
	./solanaceae/ngc_ft1/ledbat.hpp
	./solanaceae/ngc_ft1/ledbat.cpp
	./solanaceae/ngc_ft1/cubic.hpp
	./solanaceae/ngc_ft1/cubic.cpp

	./solanaceae/ngc_ft1/rcv_buf.hpp
	./solanaceae/ngc_ft1/rcv_buf.cpp
	./solanaceae/ngc_ft1/snd_buf.hpp
	./solanaceae/ngc_ft1/snd_buf.cpp
)
target_include_directories(solanaceae_ngcft1 PUBLIC .)
target_compile_features(solanaceae_ngcft1 PUBLIC cxx_std_17)
target_link_libraries(solanaceae_ngcft1 PUBLIC
	solanaceae_ngcext
)

########################################

add_library(solanaceae_sha1_ngcft1
	# hacky deps
	./solanaceae/ngc_ft1_sha1/mio.hpp
	./solanaceae/ngc_ft1_sha1/file2_mapped.hpp
	./solanaceae/ngc_ft1_sha1/file_constructor.hpp
	./solanaceae/ngc_ft1_sha1/file_constructor.cpp

	./solanaceae/ngc_ft1_sha1/backends/sha1_mapped_filesystem.hpp
	./solanaceae/ngc_ft1_sha1/backends/sha1_mapped_filesystem.cpp

	./solanaceae/ngc_ft1_sha1/hash_utils.hpp
	./solanaceae/ngc_ft1_sha1/hash_utils.cpp

	./solanaceae/ngc_ft1_sha1/util.hpp

	./solanaceae/ngc_ft1_sha1/ft1_sha1_info.hpp
	./solanaceae/ngc_ft1_sha1/ft1_sha1_info.cpp

	./solanaceae/ngc_ft1_sha1/components.hpp
	./solanaceae/ngc_ft1_sha1/components.cpp

	./solanaceae/ngc_ft1_sha1/contact_components.hpp

	./solanaceae/ngc_ft1_sha1/chunk_picker.hpp
	./solanaceae/ngc_ft1_sha1/chunk_picker.cpp

	./solanaceae/ngc_ft1_sha1/participation.hpp
	./solanaceae/ngc_ft1_sha1/participation.cpp

	./solanaceae/ngc_ft1_sha1/file_inactivity_system.hpp
	./solanaceae/ngc_ft1_sha1/file_inactivity_system.cpp

	./solanaceae/ngc_ft1_sha1/re_announce_systems.hpp
	./solanaceae/ngc_ft1_sha1/re_announce_systems.cpp

	./solanaceae/ngc_ft1_sha1/chunk_picker_systems.hpp
	./solanaceae/ngc_ft1_sha1/chunk_picker_systems.cpp

	./solanaceae/ngc_ft1_sha1/transfer_stats_systems.hpp
	./solanaceae/ngc_ft1_sha1/transfer_stats_systems.cpp

	./solanaceae/ngc_ft1_sha1/sending_transfers.hpp
	./solanaceae/ngc_ft1_sha1/sending_transfers.cpp

	./solanaceae/ngc_ft1_sha1/receiving_transfers.hpp
	./solanaceae/ngc_ft1_sha1/receiving_transfers.cpp

	./solanaceae/ngc_ft1_sha1/sha1_ngcft1.hpp
	./solanaceae/ngc_ft1_sha1/sha1_ngcft1.cpp
)
target_include_directories(solanaceae_sha1_ngcft1 PUBLIC .)
target_compile_features(solanaceae_sha1_ngcft1 PUBLIC cxx_std_17)
target_compile_definitions(solanaceae_sha1_ngcft1 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(solanaceae_sha1_ngcft1 PUBLIC
	solanaceae_ngcft1
	sha1::sha1
	solanaceae_tox_contacts
	solanaceae_message3
	solanaceae_object_store
	solanaceae_file2
)

option(SOLANACEAE_NGCFT1_SHA1_BUILD_TESTING "Build the solanaceae_ngcft1_sha1 tests" OFF)
message("II SOLANACEAE_NGCFT1_SHA1_BUILD_TESTING " ${SOLANACEAE_NGCFT1_SHA1_BUILD_TESTING})

# TODO: proper options n shit
if (SOLANACEAE_NGCFT1_SHA1_BUILD_TESTING)
	include(CTest)

	#add_executable(bitset_tests
	#    ./solanaceae/ngc_ft1_sha1/bitset_tests.cpp
	#)

	#target_link_libraries(bitset_tests PUBLIC
	#    solanaceae_sha1_ngcft1
	#)

endif()

########################################

add_library(solanaceae_ngchs2
	./solanaceae/ngc_hs2/serl.hpp

	./solanaceae/ngc_hs2/ts_find_start.hpp

	./solanaceae/ngc_hs2/ngc_hs2_sigma.hpp
	./solanaceae/ngc_hs2/ngc_hs2_sigma.cpp

	./solanaceae/ngc_hs2/ngc_hs2_rizzler.hpp
	./solanaceae/ngc_hs2/ngc_hs2_rizzler.cpp
)
target_include_directories(solanaceae_ngchs2 PUBLIC .)
target_compile_features(solanaceae_ngchs2 PUBLIC cxx_std_17)
target_link_libraries(solanaceae_ngchs2 PUBLIC
	solanaceae_ngcft1
	solanaceae_sha1_ngcft1 # HACK: properly abstract filekind/-id
	solanaceae_tox_contacts
	solanaceae_message3
	solanaceae_object_store
	nlohmann_json::nlohmann_json
)

option(SOLANACEAE_NGCHS2_BUILD_TESTING "Build the solanaceae_ngchs2 tests" OFF)
message("II SOLANACEAE_NGCHS2_BUILD_TESTING " ${SOLANACEAE_NGCHS2_BUILD_TESTING})

if (SOLANACEAE_NGCHS2_BUILD_TESTING)
	include(CTest)

	add_executable(test_hs2_ts_binarysearch
		./solanaceae/ngc_hs2/test_ts_binarysearch.cpp
	)

	target_link_libraries(test_hs2_ts_binarysearch PUBLIC
		solanaceae_ngchs2
	)

	add_test(NAME test_hs2_ts_binarysearch COMMAND test_hs2_ts_binarysearch)

endif()

########################################

if (SOLANACEAE_NGCFT1_BUILD_PLUGINS)
	add_subdirectory(./plugins)
endif()

