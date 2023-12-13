#pragma once

#include <solanaceae/message3/file.hpp>

#include "./mio.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

struct FileRWMapped : public FileI {
	mio::ummap_sink _file_map;

	// TODO: add truncate support?
	FileRWMapped(std::string_view file_path, uint64_t file_size) {
		_file_size = file_size;
		std::filesystem::path native_file_path{file_path};

		if (!std::filesystem::exists(native_file_path)) {
			std::ofstream(native_file_path) << '\0'; // force create the file
		}
		std::filesystem::resize_file(native_file_path, file_size); // ensure size, usually sparse

		std::error_code err;
		// sink, is also read
		_file_map.map(native_file_path.u8string(), 0, file_size, err);

		if (err) {
			std::cerr << "FileRWMapped error: mapping file failed " << err << "\n";
			return;
		}
	}

	virtual ~FileRWMapped(void) override {}

	bool isGood(void) override {
		return _file_map.is_mapped();
	}

	std::vector<uint8_t> read(uint64_t pos, uint64_t size) override {
		if (pos+size > _file_size) {
			//assert(false && "read past end");
			return {};
		}

		return {_file_map.data()+pos, _file_map.data()+(pos+size)};
	}

	bool write(uint64_t pos, const std::vector<uint8_t>& data) override {
		if (pos+data.size() > _file_size) {
			return false;
		}

		std::memcpy(_file_map.data()+pos, data.data(), data.size());

		return true;
	}
};

