#pragma once

#include <cstdint>
#include <vector>


#include <cassert>

// runtime sized bitset (stl so sad)
// size is round up to bytes
struct BitSet {
	std::vector<uint8_t> _bytes;

	BitSet(void) = default;
	BitSet(const BitSet&) = default;
	BitSet(BitSet&&) = default;
	BitSet(size_t size) {
		_bytes.resize((size+7)/8);
	}

	BitSet& operator=(const BitSet&) = default;
	BitSet& operator=(BitSet&&) = default;

	bool operator[](size_t pos) const {
		assert(pos < size_bits());
		if (pos >= size_bits()) {
			return false;
		}

		const size_t pos_in_bytes = pos/8;

		assert(pos_in_bytes < size_bytes());
		if (pos_in_bytes >= size_bytes()) {
			return false;
		}

		const size_t pos_rest_bits = pos%8;

		// bits are ordered high to low
		return _bytes[pos_in_bytes] & ((0x1 << 7) >> pos_rest_bits);
	}

	void set(size_t pos) {
		assert(pos < _bytes.size()*8);
		const size_t pos_in_bytes = pos/8;
		assert(pos_in_bytes < _bytes.size());

		const size_t pos_rest_bits = pos%8;

		// bits are ordered high to low
		_bytes[pos_in_bytes] |= ((0x1 << 7) >> pos_rest_bits);
	}

	void unset(size_t pos) {
		assert(pos < _bytes.size()*8);
		const size_t pos_in_bytes = pos/8;
		assert(pos_in_bytes < _bytes.size());

		const size_t pos_rest_bits = pos%8;

		// bits are ordered high to low
		_bytes[pos_in_bytes] &= ~((0x1 << 7) >> pos_rest_bits);
	}

	uint8_t* data(void) {
		return _bytes.data();
	}

	size_t size_bits(void) const {
		return _bytes.size()*8;
	}

	size_t size_bytes(void) const {
		return _bytes.size();
	}

	BitSet& merge(const BitSet& other) {
		if (other.size_bytes() > size_bytes()) {
			_bytes.resize(other.size_bytes());
		}

		for (size_t i = 0; i < size_bytes() && i < other.size_bytes(); i++) {
			_bytes[i] |= other._bytes[i];
		}

		return *this;
	}

	// start is the first bit in other relative to self
	BitSet& merge(const BitSet& other, size_t start) {
		// TODO: efficent implementation

		size_t need_size_bits = other.size_bits() + start;
		if (need_size_bits > size_bits()) {
			_bytes.resize((need_size_bits+7)/8);
		}

		for (size_t i = 0; i < other.size_bits(); i++) {
			if (other[i]) {
				set(start+i);
			}
		}

		return *this;
	}
};

