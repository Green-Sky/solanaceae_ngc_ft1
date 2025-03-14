#include "./sending_transfers.hpp"

#include <iostream>
#include <cassert>

void SendingTransfers::tick(float delta) {
	for (auto peer_it = _data.begin(); peer_it != _data.end();) {
		for (auto it = peer_it->second.begin(); it != peer_it->second.end();) {
			it->second.time_since_activity += delta;

			// if we have not heard for 10min, timeout (lower level event on real timeout)
			// (2min was too little, so it seems)
			// TODO: do we really need this if we get events?
			// FIXME: disabled for now, we are trusting ngcft1 for now
			if (false && it->second.time_since_activity >= 60.f*10.f) {
				std::cerr << "SHA1_NGCFT1 warning: sending tansfer timed out " << "." << int(it->first) << "\n";
				assert(false);
				it = peer_it->second.erase(it);
			} else {
				it++;
			}
		}

		if (peer_it->second.empty()) {
			// cleanup unused peers too agressive?
			peer_it = _data.erase(peer_it);
		} else {
			peer_it++;
		}
	}
}

SendingTransfers::Entry& SendingTransfers::emplaceInfo(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id, const Entry::Info& info) {
	auto& ent = _data[combine_ids(group_number, peer_number)][transfer_id];
	ent.v = info;
	return ent;
}

SendingTransfers::Entry& SendingTransfers::emplaceChunk(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id, const Entry::Chunk& chunk) {
	assert(!containsPeerChunk(group_number, peer_number, chunk.o, chunk.chunk_index));
	auto& ent = _data[combine_ids(group_number, peer_number)][transfer_id];
	ent.v = chunk;
	return ent;
}

bool SendingTransfers::containsPeerTransfer(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id) const {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return false;
	}

	return it->second.count(transfer_id);
}

bool SendingTransfers::containsChunk(ObjectHandle o, size_t chunk_idx) const {
	for (const auto& [_, p] : _data) {
		for (const auto& [_2, v] : p) {
			if (!v.isChunk()) {
				continue;
			}

			const auto& c = v.getChunk();
			if (c.o != o) {
				continue;
			}

			if (c.chunk_index == chunk_idx) {
				return true;
			}
		}
	}

	return false;
}

bool SendingTransfers::containsPeerChunk(uint32_t group_number, uint32_t peer_number, ObjectHandle o, size_t chunk_idx) const {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return false;
	}

	for (const auto& [_, v] : it->second) {
		if (!v.isChunk()) {
			continue;
		}

		const auto& c = v.getChunk();
		if (c.o != o) {
			continue;
		}

		if (c.chunk_index == chunk_idx) {
			return true;
		}
	}

	return false;
}

void SendingTransfers::removePeer(uint32_t group_number, uint32_t peer_number) {
	_data.erase(combine_ids(group_number, peer_number));
}

void SendingTransfers::removePeerTransfer(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id) {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return;
	}

	it->second.erase(transfer_id);
}

size_t SendingTransfers::size(void) const {
	size_t count {0};
	for (const auto& [_, p] : _data) {
		count += p.size();
	}
	return count;
}

size_t SendingTransfers::sizePeer(uint32_t group_number, uint32_t peer_number) const {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return 0;
	}

	return it->second.size();
}
