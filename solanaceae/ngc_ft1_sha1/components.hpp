#pragma once

#include <solanaceae/contact/components.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/message3/registry_message_model.hpp>

#include <solanaceae/util/bitset.hpp>

#include <entt/container/dense_set.hpp>

#include "./ft1_sha1_info.hpp"
#include "./hash_utils.hpp"

#include <vector>


// TODO: rename to object components
namespace Components {

	struct Messages {
		// dense set instead?
		std::vector<Message3Handle> messages;
	};

	using FT1InfoSHA1 = FT1InfoSHA1;

	struct FT1InfoSHA1Data {
		std::vector<uint8_t> data;
	};

	struct FT1InfoSHA1Hash {
		std::vector<uint8_t> hash;
	};

	struct FT1ChunkSHA1Cache {
		//std::vector<bool> have_chunk;
		// have_chunk is the size of info.chunks.size(), or empty if have_all
		// keep in mind bitset rounds up to 8s
		BitSet have_chunk{0};

		bool have_all {false};
		size_t have_count {0};
		entt::dense_map<SHA1Digest, std::vector<size_t>> chunk_hash_to_index;

		std::vector<size_t> chunkIndices(const SHA1Digest& hash) const;
		bool haveChunk(const SHA1Digest& hash) const;
	};

	struct FT1ChunkSHA1Requested {
		// requested chunks with a timer since last request
		entt::dense_map<size_t, float> chunks;
	};

	// TODO: once announce is shipped, remove the "Suspected"
	struct SuspectedParticipants {
		entt::dense_set<Contact3> participants;
	};

	struct ReRequestInfoTimer {
		float timer {0.f};
	};

	struct ReadHeadHint {
		// points to the first byte we want
		// this is just a hint, that can be set from outside
		// to guide the sequential "piece picker" strategy
		// the strategy *should* set this to the first byte we dont yet have
		uint64_t offset_into_file {0u};
	};

} // Components

