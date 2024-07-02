#include "./receiving_transfers.hpp"

#include <iostream>

void ReceivingTransfers::tick(float delta) {
	for (auto peer_it = _data.begin(); peer_it != _data.end();) {
		for (auto it = peer_it->second.begin(); it != peer_it->second.end();) {
			it->second.time_since_activity += delta;

			// if we have not heard for 20sec, timeout
			if (it->second.time_since_activity >= 20.f) {
				std::cerr << "SHA1_NGCFT1 warning: receiving tansfer timed out " << "." << int(it->first) << "\n";
				// TODO: if info, requeue? or just keep the timer comp? - no, timer comp will continue ticking, even if loading
				//it->second.v
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

ReceivingTransfers::Entry& ReceivingTransfers::emplaceInfo(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id, const Entry::Info& info) {
	auto& ent = _data[combine_ids(group_number, peer_number)][transfer_id];
	ent.v = info;
	return ent;
}

ReceivingTransfers::Entry& ReceivingTransfers::emplaceChunk(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id, const Entry::Chunk& chunk) {
	assert(!chunk.chunk_indices.empty());
	assert(!containsPeerChunk(group_number, peer_number, chunk.content, chunk.chunk_indices.front()));
	auto& ent = _data[combine_ids(group_number, peer_number)][transfer_id];
	ent.v = chunk;
	return ent;
}

bool ReceivingTransfers::containsPeerTransfer(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id) const {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return false;
	}

	return it->second.count(transfer_id);
}

bool ReceivingTransfers::containsChunk(ObjectHandle o, size_t chunk_idx) const {
	for (const auto& [_, p] : _data) {
		for (const auto& [_2, v] : p) {
			if (!v.isChunk()) {
				continue;
			}

			const auto& c = v.getChunk();
			if (c.content != o) {
				continue;
			}

			for (const auto idx : c.chunk_indices) {
				if (idx == chunk_idx) {
					return true;
				}
			}
		}
	}

	return false;
}

bool ReceivingTransfers::containsPeerChunk(uint32_t group_number, uint32_t peer_number, ObjectHandle o, size_t chunk_idx) const {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return false;
	}

	for (const auto& [_, v] : it->second) {
		if (!v.isChunk()) {
			continue;
		}

		const auto& c = v.getChunk();
		if (c.content != o) {
			continue;
		}

		for (const auto idx : c.chunk_indices) {
			if (idx == chunk_idx) {
				return true;
			}
		}
	}

	return false;
}

void ReceivingTransfers::removePeer(uint32_t group_number, uint32_t peer_number) {
	_data.erase(combine_ids(group_number, peer_number));
}

void ReceivingTransfers::removePeerTransfer(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id) {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return;
	}

	it->second.erase(transfer_id);
}

size_t ReceivingTransfers::size(void) const {
	size_t count {0};
	for (const auto& [_, p] : _data) {
		count += p.size();
	}
	return count;
}

size_t ReceivingTransfers::sizePeer(uint32_t group_number, uint32_t peer_number) const {
	auto it = _data.find(combine_ids(group_number, peer_number));
	if (it == _data.end()) {
		return 0;
	}

	return it->second.size();
}

