#pragma once

#include <algorithm>
#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/object_store/object_store.hpp>

#include "./components.hpp"

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>

#include <cstddef>
#include <cstdint>

#include <iostream>

//#include <solanaceae/ngc_ft1/ngcft1.hpp>

// goal is to always keep 2 transfers running and X(6) requests queued up
// per peer

// contact component?
struct ChunkPicker {
	// max transfers
	static constexpr size_t max_tf_info_requests {1};
	static constexpr size_t max_tf_chunk_requests {2};

	// max outstanding requests
	// TODO: should this include transfers?
	static constexpr size_t max_open_info_requests {1};
	const size_t max_open_chunk_requests {6};

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
	std::vector<ContentChunkR> updateChunkRequests(
		Contact3Handle c,
		ObjectRegistry& objreg,
		size_t num_requests
		//NGCFT1& nft
	) {
		std::vector<ContentChunkR> req_ret;

		// count running tf and open requests
		// TODO: replace num_requests with this logic

		// while n < X
		while (false && !participating_unfinished.empty()) {
			// round robin content (remember last obj)
			if (!objreg.valid(participating_in_last)) {
				participating_in_last = participating_unfinished.begin()->first;
				//participating_in_last = *participating_unfinished.begin();
			}
			assert(objreg.valid(participating_in_last));

			auto it = participating_unfinished.find(participating_in_last);
			// hard limit robin rounds to array size time 100
			for (size_t i = 0; req_ret.size() < num_requests && i < participating_unfinished.size()*100; i++) {
				if (it == participating_unfinished.end()) {
					it = participating_unfinished.begin();
				}

				if (it->second.skips < it->second.should_skip) {
					it->second.skips++;
					continue;
				}

				ObjectHandle o {objreg, it->first};

				// intersect self have with other have
				if (!o.all_of<Components::RemoteHave, Components::FT1ChunkSHA1Cache, Components::FT1InfoSHA1>()) {
					// rare case where no one other has anything
					continue;
				}

				const auto& cc = o.get<Components::FT1ChunkSHA1Cache>();
				if (cc.have_all) {
					std::cerr << "ChunkPicker error: completed content still in participating_unfinished!\n";
					continue;
				}

				const auto& others_have = o.get<Components::RemoteHave>().others;
				auto other_it = others_have.find(c);
				if (other_it == others_have.end()) {
					// rare case where the other is participating but has nothing
					continue;
				}

				const auto& other_have = other_it->second;

				BitSet chunk_candidates = cc.have_chunk;
				if (!other_have.have_all) {
					// AND is the same as ~(~A | ~B)
					// that means we leave chunk_candidates as (have is inverted want)
					// merge is or
					// invert at the end
					chunk_candidates
						.merge(other_have.have.invert())
						.invert();
					// TODO: add intersect for more perf
				} else {
					chunk_candidates.invert();
				}
				const auto total_chunks = o.get<Components::FT1InfoSHA1>().chunks.size();
				// TODO: trim off round up to 8, since they are now always set

				// now select (globaly) unrequested other have
				// TODO: pick strategies
				// TODO: how do we prioratize within a file?
				//  - first (walk from start (or readhead?))
				//  - random (choose random start pos and walk)
				//  - rarest (keep track of rarity and sort by that)
				//  - steaming (use read head to determain time critical chunks, potentially over requesting, first (relative to stream head) otherwise
				//    maybe look into libtorrens deadline stuff
				//  - arbitrary priority maps/functions (and combine with above in rations)

				// simple, we use first
				for (size_t i = 0; i < total_chunks && req_ret.size() < num_requests && i < chunk_candidates.size_bits(); i++) {
					if (!chunk_candidates[i]) {
						continue;
					}

					// i is a candidate we can request form peer

					// first check against double requests
					if (std::find_if(req_ret.cbegin(), req_ret.cend(), [&](const auto& x) -> bool {
						return false;
					}) != req_ret.cend()) {
						// already in return array
						// how did we get here? should we fast exit? if simple-first strat, we would want to
						continue; // skip
					}

					// TODO: also check against globally running transfers!!!


					// if nothing else blocks this, add to ret
					req_ret.push_back(ContentChunkR{o, i});
				}
			}
		}

		// -- no -- (just compat with old code, ignore)
		// if n < X
		//   optimistically request 1 chunk other does not have
		//   (don't mark es requested? or lower cooldown to re-request?)

		return req_ret;
	}

	//   - reset on disconnect?
	void resetPeer(
		Contact3Handle c
	);
};

