#include "./components.hpp"

std::vector<size_t> Components::FT1ChunkSHA1Cache::chunkIndices(const SHA1Digest& hash) const {
	const auto it = chunk_hash_to_index.find(hash);
	if (it != chunk_hash_to_index.cend()) {
		return it->second;
	} else {
		return {};
	}
}

bool Components::FT1ChunkSHA1Cache::haveChunk(const SHA1Digest& hash) const {
	if (have_all) { // short cut
		return true;
	}

	if (auto i_vec = chunkIndices(hash); !i_vec.empty()) {
		// TODO: should i test all?
		return have_chunk[i_vec.front()];
	}

	// not part of this file
	return false;
}

