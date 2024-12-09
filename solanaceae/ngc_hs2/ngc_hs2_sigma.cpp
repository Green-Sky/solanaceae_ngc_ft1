#include "./ngc_hs2_sigma.hpp"

#include <solanaceae/util/span.hpp>

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/tox_messages/msg_components.hpp>

//#include <solanaceae/tox_messages/obj_components.hpp>
// TODO: this is kinda bad, needs improvement
// use tox fileid/filekind instead !
#include <solanaceae/ngc_ft1/ngcft1_file_kind.hpp>
#include <solanaceae/ngc_ft1_sha1/components.hpp>

#include <nlohmann/json.hpp>

#include "./serl.hpp"

#include "./ts_find_start.hpp"

#include <iostream>

// https://www.youtube.com/watch?v=AdAqsgga3qo

// TODO: move to own file
namespace Components {

	struct IncommingTimeRangeRequestQueue {
		struct Entry {
			TimeRangeRequest ir;
			std::vector<uint8_t> fid;
		};
		std::deque<Entry> _queue;

		// we should remove/notadd queued requests
		// that are subsets of same or larger ranges
		void queueRequest(const TimeRangeRequest& new_request, const ByteSpan fid);
	};

	struct IncommingTimeRangeRequestRunning {
		struct Entry {
			TimeRangeRequest ir;
			std::vector<uint8_t> data; // transfer data in memory
			float last_activity {0.f};
		};
		entt::dense_map<uint8_t, Entry> _list;
	};

	void IncommingTimeRangeRequestQueue::queueRequest(const TimeRangeRequest& new_request, const ByteSpan fid) {
		// TODO: do more than exact dedupe
		for (const auto& [time_range, _] : _queue) {
			if (time_range.ts_start == new_request.ts_start && time_range.ts_end == new_request.ts_end) {
				return; // already enqueued
				// TODO: what about fid?
			}
		}

		_queue.emplace_back(Entry{
			new_request,
			std::vector<uint8_t>{fid.cbegin(), fid.cend()}
		});
	}

} // Components


NGCHS2Sigma::NGCHS2Sigma(
	Contact3Registry& cr,
	RegistryMessageModelI& rmm,
	ToxContactModel2& tcm,
	NGCFT1& nft
) :
	_cr(cr),
	_rmm(rmm),
	_tcm(tcm),
	_nft(nft),
	_nftep_sr(_nft.newSubRef(this))
{
	_nftep_sr
		.subscribe(NGCFT1_Event::recv_request)
		.subscribe(NGCFT1_Event::send_data)
		.subscribe(NGCFT1_Event::send_done)
	;
}

NGCHS2Sigma::~NGCHS2Sigma(void) {
}

float NGCHS2Sigma::iterate(float delta) {
	// limit how often we update here (new fts usually)
	if (_iterate_heat > 0.f) {
		_iterate_heat -= delta;
		return 1000.f; // return heat?
	} else {
		_iterate_heat = _iterate_cooldown;
	}

	// work request queue
	// check if already running, discard

	auto fn_iirq = [this](auto&& view) {
		for (auto&& [cv, iirq] : view.each()) {
			if (iirq._queue.empty()) {
				// TODO: remove comp?
				continue;
			}

			Contact3Handle c{_cr, cv};
			auto& iirr = c.get_or_emplace<Components::IncommingTimeRangeRequestRunning>();

			// dedup queued from running

			if (iirr._list.size() >= _max_parallel_per_peer) {
				continue;
			}

			// new ft here
			// TODO: loop? nah just 1 per tick is enough
			const auto request_entry = iirq._queue.front(); // copy
			assert(!request_entry.fid.empty());

			if (!c.all_of<Contact::Components::Parent>()) {
				iirq._queue.pop_front();
				continue; // how
			}
			const Contact3Handle group_c = {*c.registry(), c.get<Contact::Components::Parent>().parent};
			if (!c.all_of<Contact::Components::ToxGroupPeerEphemeral>()) {
				iirq._queue.pop_front();
				continue;
			}
			const auto [group_number, peer_number] = c.get<Contact::Components::ToxGroupPeerEphemeral>();

			// TODO: check allowed range here
			//_max_time_into_past_default

			// potentially heavy op
			auto data = buildChatLogFileRange(group_c, request_entry.ir.ts_start, request_entry.ir.ts_end);

			uint8_t transfer_id {0};
			if (!_nft.NGC_FT1_send_init_private(
				group_number, peer_number,
				(uint32_t)NGCFT1_file_kind::HS2_RANGE_TIME_MSGPACK,
				request_entry.fid.data(), request_entry.fid.size(),
				data.size(),
				&transfer_id,
				true // can_compress (does nothing rn)
			)) {
				// sending failed, we do not pop but wait for next iterate
				// TODO: cache data
				// TODO: fail counter
				// actually, fail probably means offline, so delete?
				continue;
			}

			assert(iirr._list.count(transfer_id) == 0);
			iirr._list[transfer_id] = {request_entry.ir, data};

			iirq._queue.pop_front();
		}
	};

	// first handle range requests on weak self
	fn_iirq(_cr.view<Components::IncommingTimeRangeRequestQueue, Contact::Components::TagSelfWeak>());

	// we could stop here, if too much is already running

	// then range on others
	fn_iirq(_cr.view<Components::IncommingTimeRangeRequestQueue>(entt::exclude_t<Contact::Components::TagSelfWeak>{}));

	_cr.view<Components::IncommingTimeRangeRequestRunning>().each(
		[delta](const auto cv, Components::IncommingTimeRangeRequestRunning& irr) {
			std::vector<uint8_t> to_remove;
			for (auto&& [ft_id, entry] : irr._list) {
				entry.last_activity += delta;
				if (entry.last_activity >= 60.f) {
					to_remove.push_back(ft_id);
				}
			}
			for (const auto it : to_remove) {
				std::cout << "NGCHS2Sigma warning: timed out ." << (int)it << "\n";
				// TODO: need a way to tell ft?
				irr._list.erase(it);
				// technically we are not supposed to timeout and instead rely on the done event
			}
		}
	);

	return 1000.f;
}

void NGCHS2Sigma::handleTimeRange(Contact3Handle c, const Events::NGCFT1_recv_request& e) {
	ByteSpan fid{e.file_id, e.file_id_size};
	// TODO: better size check
	if (fid.size != sizeof(uint64_t)+sizeof(uint64_t)) {
		std::cerr << "NGCHS2S error: range not lange enough\n";
		return;
	}

	// seconds
	uint64_t ts_start{0};
	uint64_t ts_end{0};

	// parse
	try {
		ByteSpan ts_start_bytes{fid.ptr, sizeof(uint64_t)};
		ts_start = deserlTS(ts_start_bytes);

		ByteSpan ts_end_bytes{ts_start_bytes.ptr+ts_start_bytes.size, sizeof(uint64_t)};
		ts_end = deserlTS(ts_end_bytes);
	} catch (...) {
		std::cerr << "NGCHS2S error: failed to parse range\n";
		return;
	}

	if (ts_end >= ts_start) {
		std::cerr << "NGCHS2S error: end not < start\n";
		return;
	}

	// dedupe insert into queue
	// how much overlap do we allow?
	c.get_or_emplace<Components::IncommingTimeRangeRequestQueue>().queueRequest(
		{ts_start, ts_end},
		fid
	);
}

std::vector<uint8_t> NGCHS2Sigma::buildChatLogFileRange(Contact3Handle c, uint64_t ts_start, uint64_t ts_end) {
	const Message3Registry* reg_ptr = static_cast<const RegistryMessageModelI&>(_rmm).get(c);
	if (reg_ptr == nullptr) {
		return {};
	}
	const Message3Registry& msg_reg = *reg_ptr;


	if (msg_reg.storage<Message::Components::Timestamp>() == nullptr) {
		// nothing to do here
		return {};
	}

	std::cout << "NGCHS2Sigma: building chatlog for time range " << ts_start-ts_end << "s\n";

	// convert seconds to milliseconds
	// TODO: lift out?
	ts_start *= 1000;
	ts_end *= 1000;

	//std::cout << "!!!! starting msg ts search, ts_start:" << ts_start << " ts_end:" << ts_end << "\n";

	auto ts_view = msg_reg.view<Message::Components::Timestamp>();

	// we iterate "forward", so from newest to oldest

	// start is the newest ts
	const auto ts_start_it = find_start_by_ts(ts_view, ts_start);
	// end is the oldest ts

	// we only search for the start point, because we walk to the end anyway

	auto j_array = nlohmann::json::array_t{};

	// hmm
	// maybe use other view or something?
	for (auto it = ts_start_it; it != ts_view.end(); it++) {
		const auto e = *it;
		const auto& [ts_comp] = ts_view.get(e);

		if (ts_comp.ts > ts_start) {
			std::cerr << "!!!! msg ent in view too new\n";
			continue;
		} else if (ts_comp.ts < ts_end) {
			// too old, we hit the end of the range
			break;
		}

		if (!msg_reg.all_of<
			Message::Components::ContactFrom,
			Message::Components::ContactTo,
			Message::Components::ToxGroupMessageID
		>(e)) {
			continue; // ??
		}
		if (!msg_reg.any_of<Message::Components::MessageText, Message::Components::MessageFileObject>(e)) {
			continue; // skip
		}

		const auto& [c_from_c, c_to_c] = msg_reg.get<Message::Components::ContactFrom, Message::Components::ContactTo>(e);

		if (c_to_c.c != c) {
			// message was not public
			continue;
		}

		if (!_cr.valid(c_from_c.c)) {
			continue; // ???
		}

		Contact3Handle c_from{_cr, c_from_c.c};

		if (!c_from.all_of<Contact::Components::ToxGroupPeerPersistent>()) {
			continue; // ???
		}

		if (_only_send_self_observed && msg_reg.all_of<Message::Components::SyncedBy>(e) && c.all_of<Contact::Components::Self>()) {
			if (!msg_reg.get<Message::Components::SyncedBy>(e).ts.count(c.get<Contact::Components::Self>().self)) {
				continue; // did not observe ourselfs, skip
			}
		}

		auto j_entry = nlohmann::json::object_t{};

		j_entry["ts"] = ts_comp.ts/100; // millisec -> decisec
		{
			const auto& ppk_ref = c_from.get<Contact::Components::ToxGroupPeerPersistent>().peer_key.data;
			j_entry["ppk"] = nlohmann::json::binary_t{std::vector<uint8_t>{ppk_ref.cbegin(), ppk_ref.cend()}};
		}
		j_entry["mid"] = msg_reg.get<Message::Components::ToxGroupMessageID>(e).id;

		if (msg_reg.all_of<Message::Components::TagMessageIsAction>(e)) {
			j_entry["action"] = true;
		}

		if (msg_reg.all_of<Message::Components::MessageText>(e)) {
			j_entry["text"] = msg_reg.get<Message::Components::MessageText>(e).text;
		} else if (msg_reg.any_of<Message::Components::MessageFileObject>(e)) {
			const auto& o = msg_reg.get<Message::Components::MessageFileObject>(e).o;
			if (!o) {
				continue;
			}

			// HACK: use tox fild_id and file_kind instead!!
			if (o.all_of<Components::FT1InfoSHA1Hash>()) {
				j_entry["fkind"] = NGCFT1_file_kind::HASH_SHA1_INFO;
				j_entry["fid"] = nlohmann::json::binary_t{o.get<Components::FT1InfoSHA1Hash>().hash};
			} else {
				continue; // unknown file type
			}
		}

		j_array.push_back(j_entry);
	}

	std::cout << "NGCHS2Sigma: built chat log with " << j_array.size() << " entries\n";

	return nlohmann::json::to_msgpack(j_array);
}

bool NGCHS2Sigma::onEvent(const Message::Events::MessageConstruct&) {
	return false;
}

bool NGCHS2Sigma::onEvent(const Message::Events::MessageUpdated&) {
	return false;
}

bool NGCHS2Sigma::onEvent(const Message::Events::MessageDestory&) {
	return false;
}

bool NGCHS2Sigma::onEvent(const Events::NGCFT1_recv_request& e) {
	if (
		e.file_kind != NGCFT1_file_kind::HS2_RANGE_TIME_MSGPACK
	) {
		return false; // not for us
	}

	// TODO: when is it done from queue?

	auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	if (!c) {
		return false; // how
	}

	// is other peer allowed to make requests
	//bool quick_allow {false};
	bool quick_allow {true}; // HACK: disable all restrictions for this early test
	// TODO: quick deny?
	{
		//  - tagged as weakself
		if (!quick_allow && c.all_of<Contact::Components::TagSelfWeak>()) {
			quick_allow = true;
		}

		//  - sub perm level??
		//  - out of max time range (ft specific, not a quick_allow)
	}

	if (e.file_kind == NGCFT1_file_kind::HS2_RANGE_TIME_MSGPACK) {
		handleTimeRange(c, e);
	}

	return true;
}

bool NGCHS2Sigma::onEvent(const Events::NGCFT1_send_data& e) {
	auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	if (!c) {
		return false;
	}

	if (!c.all_of<Components::IncommingTimeRangeRequestRunning>()) {
		return false;
	}

	auto& irr = c.get<Components::IncommingTimeRangeRequestRunning>();
	if (!irr._list.count(e.transfer_id)) {
		return false; // not for us (maybe)
	}

	auto& transfer = irr._list.at(e.transfer_id);
	if (transfer.data.size() < e.data_offset+e.data_size) {
		std::cerr << "NGCHS2Sigma error: ft send data larger then file???\n";
		assert(false && "how");
	}
	std::memcpy(e.data, transfer.data.data()+e.data_offset, e.data_size);
	transfer.last_activity = 0.f;

	return true;
}

bool NGCHS2Sigma::onEvent(const Events::NGCFT1_send_done& e) {
	// TODO: this will return null if the peer just disconnected
	auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	if (!c) {
		return false;
	}

	if (!c.all_of<Components::IncommingTimeRangeRequestRunning>()) {
		return false;
	}

	auto& irr = c.get<Components::IncommingTimeRangeRequestRunning>();
	if (!irr._list.count(e.transfer_id)) {
		return false; // not for us (maybe)
	}

	irr._list.erase(e.transfer_id);

	// TODO: check if we completed it
	std::cout << "NGCHS2Sigma: sent chatlog to " << e.group_number << ":" << e.peer_number << "." << (int)e.transfer_id << "\n";

	return true;
}

