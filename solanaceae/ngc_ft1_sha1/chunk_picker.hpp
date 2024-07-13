#pragma once

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/object_store/object_store.hpp>

#include "./components.hpp"

#include "./receiving_transfers.hpp"

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>

#include <cstddef>
#include <cstdint>
#include <random>

//#include <solanaceae/ngc_ft1/ngcft1.hpp>

// goal is to always keep 2 transfers running and X(6) requests queued up
// per peer

struct ChunkPickerUpdateTag {};

struct ChunkPickerTimer {
	// adds update tag on 0
	float timer {0.f};
};

// contact component?
struct ChunkPicker {
	// max transfers
	static constexpr size_t max_tf_info_requests {1};
	static constexpr size_t max_tf_chunk_requests {4}; // TODO: dynamic, function/factor of (window(delay*speed)/chunksize)

	// TODO: cheaper init? tls rng for deep seeding?
	std::minstd_rand _rng{std::random_device{}()};

	// TODO: handle with hash utils?
	struct ParticipationEntry {
		ParticipationEntry(void) {}
		ParticipationEntry(uint16_t s) : should_skip(s) {}
		// skips in round robin -> lower should_skip => higher priority
		// TODO: replace with enum value
		uint16_t should_skip {2}; // 0 high, 8 low (double each time? 0,1,2,4,8)
		uint16_t skips {0};
	};
	entt::dense_map<Object, ParticipationEntry> participating_unfinished;
	Object participating_in_last {entt::null};

	private: // TODO: properly sort
	// updates participating_unfinished
	void updateParticipation(
		Contact3Handle c,
		ObjectRegistry& objreg
	);
	public:

	// ---------- tick ----------

	//void sendInfoRequests();

	// is this like a system?
	struct ContentChunkR {
		ObjectHandle object;
		size_t chunk_index;
	};
	// returns list of chunks to request
	[[nodiscard]] std::vector<ContentChunkR> updateChunkRequests(
		Contact3Handle c,
		ObjectRegistry& objreg,
		const ReceivingTransfers& rt,
		const size_t open_requests
		//const size_t flow_window
		//NGCFT1& nft
	);
};

