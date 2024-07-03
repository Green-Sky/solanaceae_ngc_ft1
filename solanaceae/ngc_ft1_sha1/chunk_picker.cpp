#include "./chunk_picker.hpp"

#include <solanaceae/tox_contacts/components.hpp>

#include "./components.hpp"

#include <algorithm>

#include <iostream>


void ChunkPicker::updateParticipation(
	Contact3Handle c,
	ObjectRegistry& objreg
) {
	// replaces them in place
	participating.clear();
	participating_unfinished.clear();

	for (const Object ov : objreg.view<Components::SuspectedParticipants>()) {
		const ObjectHandle o {objreg, ov};

		participating.emplace(o);

		if (!o.all_of<Components::FT1ChunkSHA1Cache, Components::FT1InfoSHA1>()) {
			continue;
		}

		if (!o.get<Components::FT1ChunkSHA1Cache>().have_all) {
			participating_unfinished.emplace(o, ParticipationEntry{});
		}
	}
}

std::vector<ChunkPicker::ContentChunkR> ChunkPicker::updateChunkRequests(
	Contact3Handle c,
	ObjectRegistry& objreg,
	ReceivingTransfers& rt
	//NGCFT1& nft
) {
	if (!static_cast<bool>(c)) {
		assert(false); return {};
	}

	if (!c.all_of<Contact::Components::ToxGroupPeerEphemeral>()) {
		assert(false); return {};
	}
	const auto [group_number, peer_number] = c.get<Contact::Components::ToxGroupPeerEphemeral>();

	std::vector<ContentChunkR> req_ret;

	// count running tf and open requests
	const size_t num_ongoing_transfers = rt.sizePeer(group_number, peer_number);
	// TODO: account for open requests
	// TODO: base max on rate(chunks per sec), gonna be ass with variable chunk size
	const size_t num_requests = std::max<int64_t>(0, max_tf_chunk_requests-num_ongoing_transfers);

	// while n < X

	if (participating_unfinished.empty()) {
		participating_in_last = entt::null;
		return {};
	}

	// round robin content (remember last obj)
	if (!objreg.valid(participating_in_last) || !participating_unfinished.count(participating_in_last)) {
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
		auto& requested_chunks = o.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks;

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

			// second check against global requests (this might differ based on strat)
			if (requested_chunks.count(i) != 0) {
				continue;
			}

			// third we check against globally running transfers (this might differ based on strat)
			if (rt.containsChunk(o, i)) {
				continue;
			}

			// if nothing else blocks this, add to ret
			req_ret.push_back(ContentChunkR{o, i});

			assert(requested_chunks.count(i) == 0);
			requested_chunks[i] = 0.f;
		}
	}

	// -- no -- (just compat with old code, ignore)
	// if n < X
	//   optimistically request 1 chunk other does not have
	//   (don't mark es requested? or lower cooldown to re-request?)

	return req_ret;
}

