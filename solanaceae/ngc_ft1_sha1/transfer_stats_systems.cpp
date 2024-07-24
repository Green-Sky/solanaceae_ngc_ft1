#include "./transfer_stats_systems.hpp"

#include "./components.hpp"
#include <solanaceae/object_store/meta_components_file.hpp>

#include <iostream>

namespace Systems {

void transfer_tally_update(ObjectRegistry& os_reg, const float time_now) {
	std::vector<Object> tally_to_remove;
	// for each tally -> stats separated
	os_reg.view<Components::TransferStatsTally>().each([&os_reg, time_now, &tally_to_remove](const auto ov, Components::TransferStatsTally& tally_comp) {
		// for each peer
		std::vector<Contact3> to_remove;
		for (auto&& [peer_c, peer] : tally_comp.tally) {
			auto& tss = os_reg.get_or_emplace<Components::TransferStatsSeparated>(ov).stats;

			// special logic
			// if newest older than 2sec
			//   discard

			if (!peer.recently_sent.empty()) {
				if (time_now - peer.recently_sent.back().time_point >= 2.f) {
					// clean up stale
					auto peer_in_stats_it = tss.find(peer_c);
					if (peer_in_stats_it != tss.end()) {
						peer_in_stats_it->second.rate_up = 0.f;
					}

					peer.recently_sent.clear();
					if (peer.recently_received.empty()) {
						to_remove.push_back(peer_c);
					}
				} else {
					// else trim too old front
					peer.trimSent(time_now);

					size_t tally_bytes {0u};
					for (auto& [time, bytes, accounted] : peer.recently_sent) {
						if (!accounted) {
							tss[peer_c].total_up += bytes;
							accounted = true;
						}
						tally_bytes += bytes;
					}

					tss[peer_c].rate_up = tally_bytes / (time_now - peer.recently_sent.front().time_point + 0.00001f);
				}
			}

			if (!peer.recently_received.empty()) {
				if (time_now - peer.recently_received.back().time_point >= 2.f) {
					// clean up stale
					auto peer_in_stats_it = tss.find(peer_c);
					if (peer_in_stats_it != tss.end()) {
						peer_in_stats_it->second.rate_down = 0.f;
					}

					peer.recently_received.clear();
					if (peer.recently_sent.empty()) {
						to_remove.push_back(peer_c);
					}
				} else {
					// else trim too old front
					peer.trimReceived(time_now);

					size_t tally_bytes {0u};
					for (auto& [time, bytes, accounted] : peer.recently_received) {
						if (!accounted) {
							tss[peer_c].total_down += bytes;
							accounted = true;
						}
						tally_bytes += bytes;
					}

					tss[peer_c].rate_down = tally_bytes / (time_now - peer.recently_received.front().time_point + 0.00001f);
				}
			}
		}

		for (const auto c : to_remove) {
			tally_comp.tally.erase(c);
		}

		if (tally_comp.tally.empty()) {
			tally_to_remove.push_back(ov);
		}
	});

	// for each stats separated -> stats (total)
	os_reg.view<Components::TransferStatsSeparated, Components::TransferStatsTally>().each([&os_reg](const auto ov, Components::TransferStatsSeparated& tss_comp, const auto&) {
		auto& stats = os_reg.get_or_emplace<ObjComp::Ephemeral::File::TransferStats>(ov);
		stats = {}; // reset

		for (const auto& [_, peer_stats] : tss_comp.stats) {
			stats.rate_up += peer_stats.rate_up;
			stats.rate_down += peer_stats.rate_down;
			stats.total_up += peer_stats.total_up;
			stats.total_down += peer_stats.total_down;
		}

#if 0
		std::cout << "updated stats:\n"
			<< "  rate  u:" << stats.rate_up/1024 << "KiB/s d:" << stats.rate_down/1024 << "KiB/s\n"
			<< "  total u:" << stats.total_up/1024 << "KiB d:" << stats.total_down/1024 << "KiB\n"
		;
#endif
	});


	for (const auto ov : tally_to_remove) {
		os_reg.remove<Components::TransferStatsTally>(ov);
	}
}

} // Systems

