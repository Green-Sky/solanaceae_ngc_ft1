#pragma once

#include <solanaceae/message3/file.hpp>

#include "./mio.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>

struct FileRWMapped : public FileI {
	mio::ummap_sink _file_map;

	// TODO: add truncate support?
	FileRWMapped(std::string_view file_path, uint64_t file_size) {
		_file_size = file_size;

		if (!std::filesystem::exists(file_path)) {
			std::ofstream(std::string{file_path}) << '\0'; // force create the file
		}
		std::filesystem::resize_file(file_path, file_size); // ensure size, usually sparse

		std::error_code err;
		// sink, is also read
		//_file_map = mio::make_mmap_sink(file_path, 0, file_size, err);
		//_file_map = mio::make_mmap<mio::ummap_sink>(file_path, 0, file_size, err);
		_file_map.map(file_path, 0, file_size, err);

		if (err) {
			// TODO: errro
			return;
		}
	}

	virtual ~FileRWMapped(void) {}

	bool isGood(void) override {
		return _file_map.is_mapped();
	}

	std::vector<uint8_t> read(uint64_t pos, uint32_t size) override {
		if (pos+size > _file_size) {
			return {};
		}

		return {_file_map.data()+pos, _file_map.data()+pos+size};
	}

	bool write(uint64_t pos, const std::vector<uint8_t>& data) override {
		if (pos+data.size() > _file_size) {
			return false;
		}

		std::memcpy(_file_map.data()+pos, data.data(), data.size());

		return true;
	}
};

