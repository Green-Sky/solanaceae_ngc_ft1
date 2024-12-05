cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

add_subdirectory(./sha1)

# we are running a custom msgpack serialization for hs2
if (NOT TARGET nlohmann_json::nlohmann_json)
	FetchContent_Declare(json
		URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
		URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
		EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(json)
endif()

