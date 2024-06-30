#pragma once

#include <solanaceae/object_store/object_store.hpp>

#include <entt/container/dense_map.hpp>

#include <cstdint>
#include <variant>
#include <vector>

struct ReceivingTransfers {
	struct Entry {
		struct Info {
			ObjectHandle content;
			// copy of info data
			// too large?
			std::vector<uint8_t> info_data;
		};

		struct Chunk {
			ObjectHandle content;
			std::vector<size_t> chunk_indices;
			// or data?
			// if memmapped, this would be just a pointer
		};

		std::variant<Info, Chunk> v;

		float time_since_activity {0.f};

		bool isInfo(void) const { return std::holds_alternative<Info>(v); }
		bool isChunk(void) const { return std::holds_alternative<Chunk>(v); }

		Info& getInfo(void) { return std::get<Info>(v); }
		const Info& getInfo(void) const { return std::get<Info>(v); }
		Chunk& getChunk(void) { return std::get<Chunk>(v); }
		const Chunk& getChunk(void) const { return std::get<Chunk>(v); }
	};

	// key is groupid + peerid
	// TODO: replace with contact
	//using ReceivingTransfers = entt::dense_map<uint64_t, entt::dense_map<uint8_t, ReceivingTransferE>>;
	entt::dense_map<uint64_t, entt::dense_map<uint8_t, Entry>> _data;

	void tick(float delta);

	Entry& emplaceInfo(uint64_t combined_id, uint8_t transfer_id, const Entry::Info& info);
	Entry& emplaceChunk(uint64_t combined_id, uint8_t transfer_id, const Entry::Chunk& chunk);

	bool containsPeer(uint64_t combined_id) const { return _data.count(combined_id); }
	bool containsPeerTransfer(uint64_t combined_id, uint8_t transfer_id) const;
	bool containsChunk(ObjectHandle o, size_t chunk_idx) const;
	bool containsPeerChunk(uint64_t combined_id, ObjectHandle o, size_t chunk_idx) const;

	auto& getPeer(uint64_t combined_id) { return _data.at(combined_id); }
	auto& getTransfer(uint64_t combined_id, uint8_t transfer_id) { return getPeer(combined_id).at(transfer_id); }

	void removePeer(uint64_t combined_id);
	void removePeerTransfer(uint64_t combined_id, uint8_t transfer_id);

	size_t size(void) const;
};

