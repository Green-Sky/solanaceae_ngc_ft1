#include "./re_announce_systems.hpp"

#include "./components.hpp"
#include <solanaceae/message3/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/ngc_ft1/ngcft1_file_kind.hpp>
#include <vector>
#include <cassert>

namespace Systems {

void re_announce(
	ObjectRegistry& os_reg,
	Contact3Registry& cr,
	NGCEXTEventProvider& neep,
	const float delta
) {
	std::vector<Object> to_remove;
	os_reg.view<Components::ReAnnounceTimer>().each([&os_reg, &cr, &neep, &to_remove, delta](Object ov, Components::ReAnnounceTimer& rat) {
		ObjectHandle o{os_reg, ov};
		// if paused -> remove
		if (o.all_of<Message::Components::Transfer::TagPaused>()) {
			to_remove.push_back(ov);
			return;
		}

		// if not downloading or info incomplete -> remove
		if (!o.all_of<Components::FT1ChunkSHA1Cache, Components::FT1InfoSHA1Hash, Components::AnnounceTargets>()) {
			to_remove.push_back(ov);
			assert(false && "transfer in broken state");
			return;
		}

		if (o.get<Components::FT1ChunkSHA1Cache>().have_all) {
			// transfer done, we stop announcing
			to_remove.push_back(ov);
			return;
		}


		// update all timers
		rat.timer -= delta;

		// send announces
		if (rat.timer <= 0.f) {
			rat.reset(); // exponential back-off

			std::vector<uint8_t> announce_id;
			const uint32_t file_kind = static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO);
			for (size_t i = 0; i < sizeof(file_kind); i++) {
				announce_id.push_back((file_kind>>(i*8)) & 0xff);
			}
			assert(o.all_of<Components::FT1InfoSHA1Hash>());
			const auto& info_hash = o.get<Components::FT1InfoSHA1Hash>().hash;
			announce_id.insert(announce_id.cend(), info_hash.cbegin(), info_hash.cend());

			for (const auto cv : o.get<Components::AnnounceTargets>().targets) {
				if (cr.all_of<Contact::Components::ToxGroupPeerEphemeral>(cv)) {
					// private ?
					const auto [group_number, peer_number] = cr.get<Contact::Components::ToxGroupPeerEphemeral>(cv);
					neep.send_pc1_announce(group_number, peer_number, announce_id.data(), announce_id.size());
				} else if (cr.all_of<Contact::Components::ToxGroupEphemeral>(cv)) {
					// public
					const auto group_number = cr.get<Contact::Components::ToxGroupEphemeral>(cv).group_number;
					neep.send_all_pc1_announce(group_number, announce_id.data(), announce_id.size());
				} else {
					assert(false && "we dont know how to announce to this target");
				}
			}
		}
	});

	for (const auto ov : to_remove) {
		os_reg.remove<Components::ReAnnounceTimer>(ov);
		// we keep the annouce target list around (if it exists)
		// TODO: should we make the target list more generic?
	}

	// TODO: how to handle unpause?
}

} // Systems

