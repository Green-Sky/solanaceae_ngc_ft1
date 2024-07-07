#pragma once

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/object_store/object_store.hpp>

#include "./components.hpp"

#include "./receiving_transfers.hpp"

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>

#include <cstddef>
#include <cstdint>

//#include <solanaceae/ngc_ft1/ngcft1.hpp>

// goal is to always keep 2 transfers running and X(6) requests queued up
// per peer

// contact component?
struct ChunkPicker {
	// max transfers
	static constexpr size_t max_tf_info_requests {1};
	static constexpr size_t max_tf_chunk_requests {3};

	//// max outstanding requests
	//// TODO: should this include transfers?
	//static constexpr size_t max_open_info_requests {1};
	//const size_t max_open_chunk_requests {6};

	// TODO: handle with hash utils?
	struct ParticipationEntry {
		ParticipationEntry(void) {}
		// skips in round robin -> lower should_skip => higher priority
		uint16_t should_skip {2}; // 0 high, 8 low (double each time? 0,1,2,4,8)
		uint16_t skips {2};
	};
	// TODO: only unfinished?
	entt::dense_map<Object, ParticipationEntry> participating_unfinished;
	entt::dense_set<Object> participating;
	Object participating_in_last {entt::null};

	void updateParticipation(
		Contact3Handle c,
		ObjectRegistry& objreg
	);

	// tick
	//void sendInfoRequests();
	// is this like a system?
	// TODO: only update on:
	//   - transfer start?
	//   - transfer done
	//   - request timed out
	//   - reset on disconnect?
	struct ContentChunkR {
		ObjectHandle object;
		size_t chunk_index;
	};
	// returns list of chunks to request
	[[nodiscard]] std::vector<ContentChunkR> updateChunkRequests(
		Contact3Handle c,
		ObjectRegistry& objreg,
		ReceivingTransfers& rt,
		const size_t open_requests
		//NGCFT1& nft
	);

	//   - reset on disconnect?
	void resetPeer(
		Contact3Handle c
	);
};

