#include "./components.hpp"

namespace Components {

std::vector<size_t> FT1ChunkSHA1Cache::chunkIndices(const SHA1Digest& hash) const {
	const auto it = chunk_hash_to_index.find(hash);
	if (it != chunk_hash_to_index.cend()) {
		return it->second;
	} else {
		return {};
	}
}

bool FT1ChunkSHA1Cache::haveChunk(const SHA1Digest& hash) const {
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

void ReAnnounceTimer::set(const float new_timer) {
	timer = new_timer;
	last_max = new_timer;
}

void ReAnnounceTimer::reset(void) {
	if (last_max <= 0.01f) {
		last_max = 1.f;
	}

	last_max *= 2.f;
	timer = last_max;
}

void ReAnnounceTimer::lower(void) {
	timer *= 0.1f;
	last_max *= 0.1f;
}

void TransferStatsTally::Peer::trimSent(const float time_now) {
	while (recently_sent.size() > 4 && time_now - recently_sent.front().time_point > 1.f) {
		recently_sent.pop_front();
	}
}

void TransferStatsTally::Peer::trimReceived(const float time_now) {
	while (recently_received.size() > 4 && time_now - recently_received.front().time_point > 1.f) {
		recently_received.pop_front();
	}
}

} // Components

