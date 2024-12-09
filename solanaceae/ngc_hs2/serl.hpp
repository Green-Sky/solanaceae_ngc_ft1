#pragma once

#include <solanaceae/util/span.hpp>

#include <cstdint>

template<typename Type>
static uint64_t deserlSimpleType(ByteSpan bytes) {
	if (bytes.size < sizeof(Type)) {
		throw int(1);
	}

	Type value{};

	for (size_t i = 0; i < sizeof(Type); i++) {
		value |= Type(bytes[i]) << (i*8);
	}

	return value;
}

static uint64_t deserlTS(ByteSpan ts_bytes) {
	return deserlSimpleType<uint64_t>(ts_bytes);
}

template<typename Type>
static void serlSimpleType(std::vector<uint8_t>& bytes, const Type& value) {
	for (size_t i = 0; i < sizeof(Type); i++) {
		bytes.push_back(uint8_t(value >> (i*8) & 0xff));
	}
}

