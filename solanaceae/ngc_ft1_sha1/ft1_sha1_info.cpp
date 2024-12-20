#include "./ft1_sha1_info.hpp"

// next power of two
#include <entt/core/memory.hpp>

#include <sodium.h>

SHA1Digest::SHA1Digest(const std::vector<uint8_t>& v) {
	assert(v.size() == data.size());
	for (size_t i = 0; i < data.size(); i++) {
		data[i] = v[i];
	}
}

SHA1Digest::SHA1Digest(const uint8_t* d, size_t s) {
	assert(s == data.size());
	for (size_t i = 0; i < data.size(); i++) {
		data[i] = d[i];
	}
}

std::ostream& operator<<(std::ostream& out, const SHA1Digest& v) {
	std::string str{};
	str.resize(v.size()*2, '?');

	// HECK, std is 1 larger than size returns ('\0')
	sodium_bin2hex(str.data(), str.size()+1, v.data.data(), v.data.size());

	out << str;

	return out;
}

uint32_t chunkSizeFromFileSize(uint64_t file_size) {
	const uint64_t fs_low {UINT64_C(512)*1024};
	const uint64_t fs_high {UINT64_C(2)*1024*1024*1024};

	const uint32_t cs_low {32*1024};
	const uint32_t cs_high {4*1024*1024};

	if (file_size <= fs_low) { // 512kib
		return cs_low; // 32kib
	} else if (file_size >= fs_high) { // 2gib
		return cs_high; // 4mib
	}

	double t = file_size - fs_low;
	t /= fs_high;

	double x = (1 - t) * cs_low + t * cs_high;

	return entt::next_power_of_two(uint64_t(x));
}

size_t FT1InfoSHA1::chunkSize(size_t chunk_index) const {
	if (chunk_index+1 == chunks.size()) {
		// last chunk
		return file_size - (uint64_t(chunk_index) * uint64_t(chunk_size));
	} else {
		return chunk_size;
	}
}

std::vector<uint8_t> FT1InfoSHA1::toBuffer(void) const {
	std::vector<uint8_t> buffer;
	buffer.reserve(256+8+4+20*chunks.size());

	assert(!file_name.empty());
	// TODO: optimize
	for (size_t i = 0; i < 256; i++) {
		if (i < file_name.size()) {
			buffer.push_back(file_name.at(i));
		} else {
			buffer.push_back(0);
		}
	}
	assert(buffer.size() == 256);

	{ // HACK: endianess
		buffer.push_back((file_size>>(0*8)) & 0xff);
		buffer.push_back((file_size>>(1*8)) & 0xff);
		buffer.push_back((file_size>>(2*8)) & 0xff);
		buffer.push_back((file_size>>(3*8)) & 0xff);
		buffer.push_back((file_size>>(4*8)) & 0xff);
		buffer.push_back((file_size>>(5*8)) & 0xff);
		buffer.push_back((file_size>>(6*8)) & 0xff);
		buffer.push_back((file_size>>(7*8)) & 0xff);
	}
	assert(buffer.size() == 256+8);

	// chunk size
	{ // HACK: endianess
		buffer.push_back((chunk_size>>(0*8)) & 0xff);
		buffer.push_back((chunk_size>>(1*8)) & 0xff);
		buffer.push_back((chunk_size>>(2*8)) & 0xff);
		buffer.push_back((chunk_size>>(3*8)) & 0xff);
	}

	assert(buffer.size() == 256+8+4);

	for (const auto& chunk : chunks) {
		for (size_t i = 0; i < chunk.data.size(); i++) {
			buffer.push_back(chunk.data[i]);
		}
	}
	assert(buffer.size() == 256+8+4+20*chunks.size());

	return buffer;
}

void FT1InfoSHA1::fromBuffer(const std::vector<uint8_t>& buffer) {
	assert(buffer.size() >= 256+8+4);

	// TODO: optimize
	file_name.clear();
	for (size_t i = 0; i < 256; i++) {
		char next_char = static_cast<char>(buffer[i]);
		if (next_char == 0) {
			break;
		}
		file_name.push_back(next_char);
	}

	{ // HACK: endianess
		file_size = 0;
		file_size |= uint64_t(buffer[256+0]) << (0*8);
		file_size |= uint64_t(buffer[256+1]) << (1*8);
		file_size |= uint64_t(buffer[256+2]) << (2*8);
		file_size |= uint64_t(buffer[256+3]) << (3*8);
		file_size |= uint64_t(buffer[256+4]) << (4*8);
		file_size |= uint64_t(buffer[256+5]) << (5*8);
		file_size |= uint64_t(buffer[256+6]) << (6*8);
		file_size |= uint64_t(buffer[256+7]) << (7*8);
	}

	{ // HACK: endianess
		chunk_size = 0;
		chunk_size |= uint32_t(buffer[256+8+0]) << (0*8);
		chunk_size |= uint32_t(buffer[256+8+1]) << (1*8);
		chunk_size |= uint32_t(buffer[256+8+2]) << (2*8);
		chunk_size |= uint32_t(buffer[256+8+3]) << (3*8);
	}

	assert((buffer.size()-(256+8+4)) % 20 == 0);

	for (size_t offset = 256+8+4; offset < buffer.size();) {
		assert(buffer.size() >= offset + 20);

		auto& chunk = chunks.emplace_back();
		for (size_t i = 0; i < chunk.size(); i++, offset++) {
			chunk.data[i] = buffer.at(offset);
		}
		// TODO: error/leftover checking
	}
}

std::ostream& operator<<(std::ostream& out, const FT1InfoSHA1& v) {
	out << "  file_name: " << v.file_name << "\n";
	out << "  file_size: " << v.file_size << "\n";
	out << "  chunk_size: " << v.chunk_size << "\n";
	out << "  chunks.size(): " << v.chunks.size() << "\n";
	return out;
}

