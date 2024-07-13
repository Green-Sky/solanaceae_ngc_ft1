#include "./chunk_picker_systems.hpp"

#include <solanaceae/ngc_ft1/ngcft1_file_kind.hpp>

#include "./components.hpp"
#include "./chunk_picker.hpp"
#include "./contact_components.hpp"

#include <cassert>
#include <iostream>

namespace Systems {

void chunk_picker_updates(
	Contact3Registry& cr,
	ObjectRegistry& os_reg,
	const entt::dense_map<Contact3, size_t>& peer_open_requests,
	const ReceivingTransfers& receiving_transfers,
	NGCFT1& nft, // TODO: remove this somehow
	const float delta
) {
	std::vector<Contact3Handle> cp_to_remove;

	// first, update timers
	cr.view<ChunkPickerTimer>().each([&cr, delta](const Contact3 cv, ChunkPickerTimer& cpt) {
		cpt.timer -= delta;
		if (cpt.timer <= 0.f) {
			cr.emplace_or_replace<ChunkPickerUpdateTag>(cv);
		}
	});

	//std::cout << "number of chunkpickers: " << _cr.storage<ChunkPicker>().size() << ", of which " << _cr.storage<ChunkPickerUpdateTag>().size() << " need updating\n";

	// now check for potentially missing cp
	auto cput_view = cr.view<ChunkPickerUpdateTag>();
	cput_view.each([&cr, &cp_to_remove](const Contact3 cv) {
		Contact3Handle c{cr, cv};

		//std::cout << "cput :)\n";

		if (!c.any_of<Contact::Components::ToxGroupPeerEphemeral, Contact::Components::FT1Participation>()) {
			std::cout << "cput uh nuh :(\n";
			cp_to_remove.push_back(c);
			return;
		}

		if (!c.all_of<ChunkPicker>()) {
			std::cout << "creating new cp!!\n";
			c.emplace<ChunkPicker>();
			c.emplace_or_replace<ChunkPickerTimer>();
		}
	});

	// now update all cp that are tagged
	cr.view<ChunkPicker, ChunkPickerUpdateTag>().each([&cr, &os_reg, &peer_open_requests, &receiving_transfers, &nft, &cp_to_remove](const Contact3 cv, ChunkPicker& cp) {
		Contact3Handle c{cr, cv};

		if (!c.all_of<Contact::Components::ToxGroupPeerEphemeral, Contact::Components::FT1Participation>()) {
			cp_to_remove.push_back(c);
			return;
		}

		//std::cout << "cpu :)\n";

		// HACK: expensive, dont do every tick, only on events
		// do verification in debug instead?
		//cp.validateParticipation(c, _os.registry());

		size_t peer_open_request = 0;
		if (peer_open_requests.contains(c)) {
			peer_open_request += peer_open_requests.at(c);
		}

		auto new_requests = cp.updateChunkRequests(
			c,
			os_reg,
			receiving_transfers,
			peer_open_request
		);

		if (new_requests.empty()) {
			// updateChunkRequests updates the unfinished
			// TODO: pull out and check there?
			if (cp.participating_unfinished.empty()) {
				std::cout << "destroying empty useless cp\n";
				cp_to_remove.push_back(c);
			} else {
				// most likely will have something soon
				// TODO: mark dirty on have instead?
				c.get_or_emplace<ChunkPickerTimer>().timer = 10.f;
			}

			return;
		}

		assert(c.all_of<Contact::Components::ToxGroupPeerEphemeral>());
		const auto [group_number, peer_number] = c.get<Contact::Components::ToxGroupPeerEphemeral>();

		for (const auto [r_o, r_idx] : new_requests) {
			auto& cc = r_o.get<Components::FT1ChunkSHA1Cache>();
			const auto& info = r_o.get<Components::FT1InfoSHA1>();

			// request chunk_idx
			nft.NGC_FT1_send_request_private(
				group_number, peer_number,
				static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_CHUNK),
				info.chunks.at(r_idx).data.data(), info.chunks.at(r_idx).size()
			);
			std::cout << "SHA1_NGCFT1: requesting chunk [" << info.chunks.at(r_idx) << "] from " << group_number << ":" << peer_number << "\n";
		}

		// force update every minute
		// TODO: add small random bias to spread load
		c.get_or_emplace<ChunkPickerTimer>().timer = 60.f;
	});

	// unmark all marked
	cr.clear<ChunkPickerUpdateTag>();
	assert(cr.storage<ChunkPickerUpdateTag>().empty());

	for (const auto& c : cp_to_remove) {
		c.remove<ChunkPicker, ChunkPickerTimer>();
	}
}

} // Systems

