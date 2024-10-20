#pragma once

#include <solanaceae/contact/components.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/object_store/meta_components_file.hpp>

#include <solanaceae/util/bitset.hpp>

#include <entt/container/dense_set.hpp>
#include <entt/container/dense_map.hpp>

#include "./ft1_sha1_info.hpp"
#include "./hash_utils.hpp"

#include <vector>
#include <deque>


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
		// TODO: extract have_count to generic comp

		// have_chunk is the size of info.chunks.size(), or empty if have_all
		// keep in mind bitset rounds up to 8s
		//BitSet have_chunk{0};

		//bool have_all {false};
		size_t have_count {0}; // move?
		entt::dense_map<SHA1Digest, std::vector<size_t>> chunk_hash_to_index;

		std::vector<size_t> chunkIndices(const SHA1Digest& hash) const;
		bool haveChunk(ObjectHandle o, const SHA1Digest& hash) const;
	};

	struct FT1File2 {
		// the cached file2 for faster access
		// should be destroyed when no activity and recreated on demand
		std::unique_ptr<File2I> file;
	};

	struct FT1ChunkSHA1Requested {
		// requested chunks with a timer since last request
		struct Entry {
			float timer {0.f};
			Contact3 c {entt::null};
		};
		entt::dense_map<size_t, Entry> chunks;
	};

	// TODO: once announce is shipped, remove the "Suspected"
	struct SuspectedParticipants {
		entt::dense_set<Contact3> participants;
	};

	struct RemoteHaveBitset {
		struct Entry {
			bool have_all {false};
			BitSet have;
		};
		entt::dense_map<Contact3, Entry> others;
	};

	struct ReRequestInfoTimer {
		float timer {0.f};
	};

	struct AnnounceTargets {
		entt::dense_set<Contact3> targets;
	};

	struct ReAnnounceTimer {
		float timer {0.f};
		float last_max {0.f};

		void set(const float new_timer);

		// exponential back-off
		void reset(void);

		// on peer join to group
		void lower(void);
	};

	struct TransferStatsSeparated {
		entt::dense_map<Contact3, ObjComp::Ephemeral::File::TransferStats> stats;
	};

	// used to populate stats
	struct TransferStatsTally {
		struct Peer {
			struct Entry {
				float time_point {0.f};
				uint64_t bytes {0u};
				bool accounted {false};
			};
			std::deque<Entry> recently_sent;
			std::deque<Entry> recently_received;

			// keep atleast 4 or 1sec
			// trim too old front
			void trimSent(const float time_now);
			void trimReceived(const float time_now);
		};
		entt::dense_map<Contact3, Peer> tally;
	};

} // Components

