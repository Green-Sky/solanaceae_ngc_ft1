#include "./bitset.hpp"

#include <cassert>
#include <iostream>
#include <random>

int main(void) {

	// ####################

	for (size_t i = 1; i <= 8; i++) {
		BitSet bs(i);
		assert(bs._bytes.size() == 1);
	}

	{
		BitSet bs(9);
		assert(bs._bytes.size() == 2);
	}

	{
		BitSet bs(24);
		assert(bs._bytes.size() == 3);
	}

	// ####################

	{ // simple bit sets in 1 byte
		BitSet bs(8);
		assert(bs._bytes.size() == 1);
		assert(bs._bytes.at(0) == 0x00);

		bs.set(0);
		assert(bs._bytes.at(0) == 0b10000000);
		assert(bs[0]);
		assert(!bs[1]);

		bs.set(7);
		assert(bs._bytes.at(0) == 0b10000001);
		assert(bs[7]);

		bs.unset(0);
		assert(bs._bytes.at(0) == 0b00000001);
		assert(!bs[0]);

		bs.set(6);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs[6]);

		// useless unset
		bs.unset(0);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(!bs[0]);

		// useless set
		bs.set(7);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs[7]);
	}

	{ // simple bit sets in 2 bytes
		BitSet bs(16);
		assert(bs._bytes.size() == 2);
		assert(bs._bytes.at(0) == 0x00);
		assert(bs._bytes.at(1) == 0x00);

		// first same as before, making sure no side effects happen

		bs.set(0);
		assert(bs._bytes.at(0) == 0b10000000);
		assert(bs._bytes.at(1) == 0b00000000);
		assert(bs[0]);

		bs.set(7);
		assert(bs._bytes.at(0) == 0b10000001);
		assert(bs._bytes.at(1) == 0b00000000);
		assert(bs[7]);

		bs.unset(0);
		assert(bs._bytes.at(0) == 0b00000001);
		assert(bs._bytes.at(1) == 0b00000000);

		bs.set(6);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b00000000);

		// useless unset
		bs.unset(0);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b00000000);

		// useless set
		bs.set(7);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b00000000);

		// now in second byte

		bs.set(8);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b10000000);
		assert(bs[8]);

		bs.set(9);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b11000000);
		assert(bs[9]);

		bs.set(15);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b11000001);
		assert(bs[15]);

		bs.unset(9);
		assert(bs._bytes.at(0) == 0b00000011);
		assert(bs._bytes.at(1) == 0b10000001);
	}

	// random long range set tests
	for (size_t iml = 0; iml < 1000; iml++) {
		std::default_random_engine rng(1337*17);

		size_t bit_pos = rng()%16384;
		size_t total_bit_count = bit_pos + rng()%16384; // so max 32768

		BitSet bs(total_bit_count);
		assert(bs.size_bytes() == (total_bit_count+7)/8);

		for (size_t i = 0; i < total_bit_count; i++) {
			assert(!bs[i]);
		}

		bs.set(bit_pos);
		assert(bs[bit_pos]);

		for (size_t i = 0; i < total_bit_count; i++) {
			if (i == bit_pos) {
				assert(bs[i]);
			} else {
				assert(!bs[i]);
			}
		}
	}

	{ // merging lets start simple
		BitSet bs1(8);
		BitSet bs2(8);
		assert(bs1._bytes.at(0) == 0b00000000);
		assert(bs2._bytes.at(0) == 0b00000000);

		bs2.set(4);
		assert(bs2._bytes.at(0) == 0b00001000);

		bs1.merge(bs2);
		assert(bs2._bytes.at(0) == 0b00001000);
		assert(bs1._bytes.at(0) == 0b00001000);
	}

	{ // merging
		BitSet bs1(8);
		BitSet bs2(8);
		assert(bs1._bytes.at(0) == 0b00000000);
		assert(bs2._bytes.at(0) == 0b00000000);

		bs1.set(1);
		bs2.set(4);
		assert(bs1._bytes.at(0) == 0b01000000);
		assert(bs2._bytes.at(0) == 0b00001000);

		bs1.merge(bs2);
		assert(bs2._bytes.at(0) == 0b00001000);
		assert(bs1._bytes.at(0) == 0b01001000);
	}

	{ // merging 2 bytes
		BitSet bs1(16);
		BitSet bs2(8);
		assert(bs1._bytes.at(0) == 0b00000000);
		assert(bs1._bytes.at(1) == 0b00000000);
		assert(bs2._bytes.at(0) == 0b00000000);

		bs1.set(1);
		bs1.set(8);
		assert(bs1._bytes.at(0) == 0b01000000);
		assert(bs1._bytes.at(1) == 0b10000000);

		bs2.set(4);
		assert(bs2._bytes.at(0) == 0b00001000);

		bs1.merge(bs2);
		assert(bs2._bytes.at(0) == 0b00001000);
		assert(bs1._bytes.at(0) == 0b01001000);
		assert(bs1._bytes.at(1) == 0b10000000);
	}

	{ // merging larger in smaller
		BitSet bs1(8);
		BitSet bs2(16);
		assert(bs1._bytes.at(0) == 0b00000000);
		assert(bs2._bytes.at(0) == 0b00000000);
		assert(bs2._bytes.at(1) == 0b00000000);

		assert(bs1.size_bytes() != bs2.size_bytes());

		bs1.set(1);
		assert(bs1._bytes.at(0) == 0b01000000);

		bs2.set(4);
		bs2.set(8);
		assert(bs2._bytes.at(0) == 0b00001000);
		assert(bs2._bytes.at(1) == 0b10000000);

		bs1.merge(bs2);
		assert(bs2._bytes.at(0) == 0b00001000);
		assert(bs2._bytes.at(1) == 0b10000000);
		assert(bs1._bytes.at(0) == 0b01001000);
		assert(bs1._bytes.at(1) == 0b10000000);

		assert(bs1.size_bytes() == bs2.size_bytes());
	}

	{ // offset merge simple
		BitSet bs1(16);
		BitSet bs2(8);
		assert(bs1._bytes.at(0) == 0b00000000);
		assert(bs1._bytes.at(1) == 0b00000000);
		assert(bs2._bytes.at(0) == 0b00000000);

		bs1.set(1);
		bs1.set(8);
		assert(bs1._bytes.at(0) == 0b01000000);
		assert(bs1._bytes.at(1) == 0b10000000);

		bs2.set(4);
		assert(bs2._bytes.at(0) == 0b00001000);

		bs1.merge(bs2, 8);
		assert(bs2._bytes.at(0) == 0b00001000);
		assert(bs1._bytes.at(0) == 0b01000000);
		assert(bs1._bytes.at(1) == 0b10001000);
	}

	return 0;
}

