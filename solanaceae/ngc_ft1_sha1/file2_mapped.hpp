#pragma once

#include <solanaceae/file/file2.hpp>

#include "./mio.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cassert>

struct File2RWMapped : public File2I {
	mio::ummap_sink _file_map;

	// TODO: add truncate support?
	// TODO: rw always true?
	File2RWMapped(std::string_view file_path, int64_t file_size = -1) : File2I(true, true) {
		std::filesystem::path native_file_path{file_path};

		if (!std::filesystem::exists(native_file_path)) {
			std::ofstream(native_file_path) << '\0'; // force create the file
		}

		_file_size = std::filesystem::file_size(native_file_path);
		if (file_size >= 0 && _file_size != file_size) {
			try {
				std::filesystem::resize_file(native_file_path, file_size); // ensure size, usually sparse
			} catch (...) {
				std::cerr << "FileRWMapped error: resizing file failed\n";
				return;
			}

			_file_size = std::filesystem::file_size(native_file_path);
			if (_file_size != file_size) {
				std::cerr << "FileRWMapped error: resizing file failed (size mismatch)\n";
				return;
			}
		}

		std::error_code err;
		// sink, is also read
		_file_map.map(native_file_path.u8string(), 0, _file_size, err);

		if (err) {
			std::cerr << "FileRWMapped error: mapping file failed: " << err.message() << " (" << err << ")\n";
			return;
		}
	}

	virtual ~File2RWMapped(void) {
	}

	bool isGood(void) override {
		return _file_map.is_mapped();
	}

	bool write(const ByteSpan data, int64_t pos = -1) override {
		// TODO: support streaming write
		if (pos < 0) {
			return false;
		}

		if (data.empty()) {
			return true; // false?
		}

		// file size is fix for mmaped files
		if (pos+data.size > _file_size) {
			return false;
		}

		std::memcpy(_file_map.data()+pos, data.ptr, data.size);

		return true;
	}

	ByteSpanWithOwnership read(uint64_t size, int64_t pos = -1) override {
		// TODO: support streaming read
		if (pos < 0) {
			assert(false && "streaming not implemented");
			return ByteSpan{};
		}

		if (pos+size > _file_size) {
			assert(false && "read past end");
			return ByteSpan{};
		}

		// return non-owning
		return ByteSpan{_file_map.data()+pos, size};
	}
};

struct File2RMapped : public File2I {
	mio::ummap_source _file_map;

	File2RMapped(std::string_view file_path) : File2I(false, true) {
		std::filesystem::path native_file_path{file_path};

		if (!std::filesystem::exists(native_file_path)) {
			std::cerr << "FileRMapped error: file does not exist\n";
			return;
		}

		_file_size = std::filesystem::file_size(native_file_path);

		std::error_code err;
		_file_map.map(native_file_path.u8string(), err);

		if (err) {
			std::cerr << "FileRMapped error: mapping file failed: " << err.message() << " (" << err << ")\n";
			return;
		}

		if (_file_size != _file_map.length()) {
			std::cerr << "FileRMapped warning: file size and mapped file size mismatch.\n";
			_file_size = _file_map.length();
		}
	}

	virtual ~File2RMapped(void) {
	}

	bool isGood(void) override {
		return _file_map.is_mapped();
	}

	bool write(const ByteSpan, int64_t = -1) override { return false; }

	ByteSpanWithOwnership read(uint64_t size, int64_t pos = -1) override {
		// TODO: support streaming read
		if (pos < 0) {
			assert(false && "streaming not implemented");
			return ByteSpan{};
		}

		if (pos+size > _file_size) {
			assert(false && "read past end");
			return ByteSpan{};
		}

		// return non-owning
		return ByteSpan{_file_map.data()+pos, size};
	}
};
