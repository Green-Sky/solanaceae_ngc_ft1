#include "./chunk_picker.hpp"

#include <solanaceae/tox_contacts/components.hpp>

#include "./components.hpp"
#include "./contact_components.hpp"

#include <algorithm>

#include <iostream>

// TODO: move ps to own file
// picker strategies are generators
// gen returns true if a valid chunk was picked
// ps should be light weight and no persistant state
// ps produce an index only once

// simply scans from the beginning, requesting chunks in that order
struct PickerStrategySequential {
	const BitSet& chunk_candidates;
	const size_t total_chunks;

	size_t i {0u};

	PickerStrategySequential(
		const BitSet& chunk_candidates_,
		const size_t total_chunks_,
		const size_t start_offset_ = 0u
	) :
		chunk_candidates(chunk_candidates_),
		total_chunks(total_chunks_),
		i(start_offset_)
	{}


	bool gen(size_t& out_chunk_idx) {
		for (; i < total_chunks && i < chunk_candidates.size_bits(); i++) {
			if (chunk_candidates[i]) {
				out_chunk_idx = i;
				i++;
				return true;
			}
		}

		return false;
	}
};

// chooses a random start position and then requests linearly from there
struct PickerStrategyRandom {
	const BitSet& chunk_candidates;
	const size_t total_chunks;
	std::minstd_rand& rng;

	size_t count {0u};
	size_t i {rng()%total_chunks};

	PickerStrategyRandom(
		const BitSet& chunk_candidates_,
		const size_t total_chunks_,
		std::minstd_rand& rng_
	) :
		chunk_candidates(chunk_candidates_),
		total_chunks(total_chunks_),
		rng(rng_)
	{}

	bool gen(size_t& out_chunk_idx) {
		for (; count < total_chunks; count++, i++) {
			// wrap around
			if (i >= total_chunks) {
				i = i%total_chunks;
			}

			if (chunk_candidates[i]) {
				out_chunk_idx = i;
				count++;
				i++;
				return true;
			}
		}

		return false;
	}
};

// switches randomly between random and sequential
struct PickerStrategyRandomSequential {
	PickerStrategyRandom psr;
	PickerStrategySequential pssf;

	// TODO: configurable
	std::bernoulli_distribution d{0.5f};

	PickerStrategyRandomSequential(
		const BitSet& chunk_candidates_,
		const size_t total_chunks_,
		std::minstd_rand& rng_,
		const size_t start_offset_ = 0u
	) :
		psr(chunk_candidates_, total_chunks_, rng_),
		pssf(chunk_candidates_, total_chunks_, start_offset_)
	{}

	bool gen(size_t& out_chunk_idx) {
		if (d(psr.rng)) {
			return psr.gen(out_chunk_idx);
		} else {
			return pssf.gen(out_chunk_idx);
		}
	}
};

// TODO: return bytes instead, so it can be done chunk size independent
static constexpr size_t flowWindowToRequestCount(size_t flow_window) {
	// based on 500KiB/s with ~0.05s delay looks fine
	// increase to 4 at wnd >= 25*1024
	if (flow_window >= 25*1024) {
		return 4u;
	}
	return 3u;
}

void ChunkPicker::updateParticipation(
	Contact3Handle c,
	ObjectRegistry& objreg
) {
	if (!c.all_of<Contact::Components::FT1Participation>()) {
		participating_unfinished.clear();
		return;
	}

	entt::dense_set<Object> checked;
	for (const Object ov : c.get<Contact::Components::FT1Participation>().participating) {
		const ObjectHandle o {objreg, ov};

		if (participating_unfinished.contains(o)) {
			if (!o.all_of<Components::FT1ChunkSHA1Cache, Components::FT1InfoSHA1>()) {
				participating_unfinished.erase(o);
				continue;
			}

			if (o.all_of<Message::Components::Transfer::TagPaused>()) {
				participating_unfinished.erase(o);
				continue;
			}

			if (o.get<Components::FT1ChunkSHA1Cache>().have_all) {
				participating_unfinished.erase(o);
			}
		} else {
			if (!o.all_of<Components::FT1ChunkSHA1Cache, Components::FT1InfoSHA1>()) {
				continue;
			}

			if (o.all_of<Message::Components::Transfer::TagPaused>()) {
				continue;
			}

			if (!o.get<Components::FT1ChunkSHA1Cache>().have_all) {
				using Priority = Components::DownloadPriority::Priority;
				Priority prio = Priority::NORMAL;

				if (o.all_of<Components::DownloadPriority>()) {
					prio = o.get<Components::DownloadPriority>().p;
				}

				uint16_t pskips =
					prio == Priority::HIGHER ? 0u :
					prio == Priority::HIGH ? 1u :
					prio == Priority::NORMAL ? 2u :
					prio == Priority::LOW ? 4u :
					8u
				;

				participating_unfinished.emplace(o, ParticipationEntry{pskips});
			}
		}
		checked.emplace(o);
	}

	// now we still need to remove left over unfinished.
	// TODO: how did they get left over
	entt::dense_set<Object> to_remove;
	for (const auto& [o, _] : participating_unfinished) {
		if (!checked.contains(o)) {
			std::cerr << "unfinished contained non participating\n";
			to_remove.emplace(o);
		}
	}
	for (const auto& o : to_remove) {
		participating_unfinished.erase(o);
	}
}

std::vector<ChunkPicker::ContentChunkR> ChunkPicker::updateChunkRequests(
	Contact3Handle c,
	ObjectRegistry& objreg,
	ReceivingTransfers& rt,
	const size_t open_requests
	//const size_t flow_window
	//NGCFT1& nft
) {
	if (!static_cast<bool>(c)) {
		assert(false); return {};
	}

	if (!c.all_of<Contact::Components::ToxGroupPeerEphemeral>()) {
		assert(false); return {};
	}
	const auto [group_number, peer_number] = c.get<Contact::Components::ToxGroupPeerEphemeral>();

	updateParticipation(c, objreg);

	if (participating_unfinished.empty()) {
		participating_in_last = entt::null;
		return {};
	}

	std::vector<ContentChunkR> req_ret;

	// count running tf and open requests
	const size_t num_ongoing_transfers = rt.sizePeer(group_number, peer_number);
	// TODO: account for open requests
	const int64_t num_total = num_ongoing_transfers + open_requests;

	// TODO: base max on rate(chunks per sec), gonna be ass with variable chunk size
	//const size_t num_max = std::max(max_tf_chunk_requests, flowWindowToRequestCount(flow_window));
	const size_t num_max = max_tf_chunk_requests;

	const size_t num_requests = std::max<int64_t>(0, int64_t(num_max)-num_total);
	std::cerr << "CP: want " << num_requests << "(rt:" << num_ongoing_transfers << " or:" << open_requests << ") from " << group_number << ":" << peer_number << "\n";

	// while n < X

	// round robin content (remember last obj)
	if (!objreg.valid(participating_in_last) || !participating_unfinished.count(participating_in_last)) {
		participating_in_last = participating_unfinished.begin()->first;
		//participating_in_last = *participating_unfinished.begin();
	}
	assert(objreg.valid(participating_in_last));

	auto it = participating_unfinished.find(participating_in_last);
	// hard limit robin rounds to array size time 20
	for (size_t i = 0; req_ret.size() < num_requests && i < participating_unfinished.size()*20; i++) {
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
		const auto& info = o.get<Components::FT1InfoSHA1>();
		const auto total_chunks = info.chunks.size();
		auto& requested_chunks = o.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks;

		// TODO: trim off round up to 8, since they are now always set

		// now select (globaly) unrequested other have
		// TODO: how do we prioritize within a file?
		//  - sequential (walk from start (or readhead?))
		//  - random (choose random start pos and walk)
		//  - random/sequential (randomly choose between the 2)
		//  - rarest (keep track of rarity and sort by that)
		//  - steaming (use readhead to determain time critical chunks, potentially over requesting, first (relative to stream head) otherwise
		//    maybe look into libtorrens deadline stuff
		//  - arbitrary priority maps/functions (and combine with above in rations)

		// TODO: configurable
		size_t start_offset {0u};
		if (o.all_of<Components::ReadHeadHint>()) {
			const auto byte_offset = o.get<Components::ReadHeadHint>().offset_into_file;
			if (byte_offset <= info.file_size) {
				start_offset = o.get<Components::ReadHeadHint>().offset_into_file/info.chunk_size;
			} else {
				// error?
			}
		}
		//PickerStrategySequential ps(chunk_candidates, total_chunks, start_offset);
		//PickerStrategyRandom ps(chunk_candidates, total_chunks, _rng);
		PickerStrategyRandomSequential ps(chunk_candidates, total_chunks, _rng, start_offset);
		size_t out_chunk_idx {0};
		while (ps.gen(out_chunk_idx) && req_ret.size() < num_requests) {
			// out_chunk_idx is a potential candidate we can request form peer

			// - check against double requests
			if (std::find_if(req_ret.cbegin(), req_ret.cend(), [&](const ContentChunkR& x) -> bool {
				return x.object == o && x.chunk_index == out_chunk_idx;
			}) != req_ret.cend()) {
				// already in return array
				// how did we get here? should we fast exit? if sequential strat, we would want to
				continue; // skip
			}

			// - check against global requests (this might differ based on strat)
			if (requested_chunks.count(out_chunk_idx) != 0) {
				continue;
			}

			// - we check against globally running transfers (this might differ based on strat)
			if (rt.containsChunk(o, out_chunk_idx)) {
				continue;
			}

			// if nothing else blocks this, add to ret
			req_ret.push_back(ContentChunkR{o, out_chunk_idx});

			// TODO: move this after packet was sent successfully
			// (move net in? hmm)
			requested_chunks[out_chunk_idx] = Components::FT1ChunkSHA1Requested::Entry{0.f, c};
		}
	}

	if (it == participating_unfinished.end() || ++it == participating_unfinished.end()) {
		participating_in_last = entt::null;
	} else {
		participating_in_last = it->first;
	}

	if (req_ret.size() < num_requests) {
		std::cerr << "CP: could not fulfil, " << group_number << ":" << peer_number << " only has " << req_ret.size() << " candidates\n";
	}

	// -- no -- (just compat with old code, ignore)
	// if n < X
	//   optimistically request 1 chunk other does not have
	//   (don't mark es requested? or lower cooldown to re-request?)

	return req_ret;
}

