cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

add_subdirectory(./external)

project(solanaceae)

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
	./solanaceae/ngc_ft1_sha1/file_rw_mapped.hpp

	./solanaceae/ngc_ft1_sha1/hash_utils.hpp
	./solanaceae/ngc_ft1_sha1/hash_utils.cpp

	./solanaceae/ngc_ft1_sha1/ft1_sha1_info.hpp
	./solanaceae/ngc_ft1_sha1/ft1_sha1_info.cpp

	./solanaceae/ngc_ft1_sha1/components.hpp
	./solanaceae/ngc_ft1_sha1/components.cpp

	./solanaceae/ngc_ft1_sha1/chunk_picker.hpp

	./solanaceae/ngc_ft1_sha1/participation.hpp
	./solanaceae/ngc_ft1_sha1/participation.cpp

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

########################################

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

