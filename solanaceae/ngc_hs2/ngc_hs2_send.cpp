#include "./ngc_hs2_send.hpp"

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

#include <algorithm>

#include <iostream>

// https://www.youtube.com/watch?v=AdAqsgga3qo

namespace Components {

void IncommingTimeRangeRequestQueue::queueRequest(const TimeRangeRequest& new_request) {
	// TODO: do more than exact dedupe
	for (const auto& [ts_start, ts_end] : _queue) {
		if (ts_start == new_request.ts_start && ts_end == new_request.ts_end) {
			return; // already enqueued
		}
	}

	_queue.push_back(new_request);
}

} // Components


NGCHS2Send::NGCHS2Send(
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

NGCHS2Send::~NGCHS2Send(void) {
}

float NGCHS2Send::iterate(float delta) {
	// limit how often we update here (new fts usually)
	if (_iterate_heat > 0.f) {
		_iterate_heat -= delta;
		return 1000.f;
	} else {
		_iterate_heat = _iterate_cooldown;
	}

	// work request queue
	// check if already running, discard

	auto fn_iirq = [this](auto&& view) {
		for (auto&& [cv, iirq] : view.each()) {
			Contact3Handle c{_cr, cv};
			auto& iirr = c.get_or_emplace<Components::IncommingTimeRangeRequestRunning>();

			// dedup queued from running

			if (iirr._list.size() >= _max_parallel_per_peer) {
				continue;
			}

			// new ft here?
		}
	};

	// first handle range requests on weak self
	//for (auto&& [cv, iirq] : _cr.view<Contact::Components::TagSelfWeak, Components::IncommingTimeRangeRequestQueue>().each()) {
	fn_iirq(_cr.view<Contact::Components::TagSelfWeak, Components::IncommingTimeRangeRequestQueue>());

	// we could stop here, if too much is already running

	// then range on others
	//for (auto&& [cv, iirq] : _cr.view<Components::IncommingTimeRangeRequestQueue>(entt::exclude_t<Contact::Components::TagSelfWeak>{}).each()) {
	fn_iirq(_cr.view<Components::IncommingTimeRangeRequestQueue>(entt::exclude_t<Contact::Components::TagSelfWeak>{}));

	return 1000.f;
}

template<typename Type>
static uint64_t deserlSimpleType(ByteSpan bytes) {
	if (bytes.size < sizeof(Type)) {
		throw int(1);
	}

	Type value;

	for (size_t i = 0; i < sizeof(Type); i++) {
		value |= Type(bytes[i]) << (i*8);
	}

	return value;
}

static uint32_t deserlMID(ByteSpan mid_bytes) {
	return deserlSimpleType<uint32_t>(mid_bytes);
}

static uint64_t deserlTS(ByteSpan ts_bytes) {
	return deserlSimpleType<uint64_t>(ts_bytes);
}

void NGCHS2Send::handleTimeRange(Contact3Handle c, const Events::NGCFT1_recv_request& e) {
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

	// dedupe insert into queue
	// how much overlap do we allow?
	c.get_or_emplace<Components::IncommingTimeRangeRequestQueue>().queueRequest({
		ts_start,
		ts_end,
	});
}

#if 0
void NGCHS2Send::handleSingleMessage(Contact3Handle c, const Events::NGCFT1_recv_request& e) {
	ByteSpan fid{e.file_id, e.file_id_size};
	// TODO: better size check
	if (fid.size != 32+sizeof(uint32_t)+sizeof(uint64_t)) {
		std::cerr << "NGCHS2S error: singlemessage not lange enough\n";
		return;
	}

	ByteSpan ppk;
	uint32_t mid {0};
	uint64_t ts {0}; // deciseconds

	// parse
	try {
		// - ppk
		// TOX_GROUP_PEER_PUBLIC_KEY_SIZE (32)
		ppk = {fid.ptr, 32};

		// - mid
		ByteSpan mid_bytes{fid.ptr+ppk.size, sizeof(uint32_t)};
		mid = deserlMID(mid_bytes);

		// - ts
		ByteSpan ts_bytes{mid_bytes.ptr+mid_bytes.size, sizeof(uint64_t)};
		ts = deserlTS(ts_bytes);
	} catch (...) {
		std::cerr << "NGCHS2S error: failed to parse singlemessage\n";
		return;
	}


	// for queue, we need group, peer, msg_ppk, msg_mid, msg_ts

	// dedupe insert into queue
	c.get_or_emplace<Components::IncommingMsgRequestQueue>().queueRequest({
		ppk,
		mid,
		ts,
	});
}
#endif

std::vector<uint8_t> NGCHS2Send::buildHSFileRange(Contact3Handle c, uint64_t ts_start, uint64_t ts_end) {
	const Message3Registry* reg_ptr = static_cast<const RegistryMessageModelI&>(_rmm).get(c);
	if (reg_ptr == nullptr) {
		return {};
	}
	const Message3Registry& msg_reg = *reg_ptr;


	if (msg_reg.storage<Message::Components::Timestamp>() == nullptr) {
		// nothing to do here
		return {};
	}

	std::cout << "!!!! starting msg ts search, ts_start:" << ts_start << " ts_end:" << ts_end << "\n";

	auto ts_view = msg_reg.view<Message::Components::Timestamp>();

	// we iterate "forward", so from newest to oldest

	// start is the newest ts
	auto ts_start_it = ts_view.end(); // start invalid
	// end is the oldest ts
	//
	{ // binary search for first value not newer than start ts
		// -> first value smaller than start ts
		auto res = std::lower_bound(
			ts_view.begin(), ts_view.end(),
			ts_start,
			[&ts_view](const auto& a, const auto& b) {
				const auto& [a_comp] = ts_view.get(a);
				return a_comp.ts > b; // > bc ts is sorted high to low?
			}
		);

		if (res != ts_view.end()) {
			const auto& [ts_comp] = ts_view.get(*res);
			std::cout << "!!!! first value not newer than start ts is " << ts_comp.ts << "\n";
			ts_start_it = res;
		} else {
			std::cout << "!!!! no first value not newer than start ts\n";
		}
	}

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

		if (msg_reg.all_of<Message::Components::MessageText>(e)) {
			if (msg_reg.all_of<Message::Components::TagMessageIsAction>(e)) {
				j_entry["msgtype"] = "action"; // TODO: textaction?
			} else {
				j_entry["msgtype"] = "text";
			}
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

	return nlohmann::json::to_msgpack(j_array);
}

bool NGCHS2Send::onEvent(const Message::Events::MessageConstruct&) {
	return false;
}

bool NGCHS2Send::onEvent(const Message::Events::MessageUpdated&) {
	return false;
}

bool NGCHS2Send::onEvent(const Message::Events::MessageDestory&) {
	return false;
}

bool NGCHS2Send::onEvent(const Events::NGCFT1_recv_request& e) {
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

bool NGCHS2Send::onEvent(const Events::NGCFT1_send_data&) {
	return false;
}

bool NGCHS2Send::onEvent(const Events::NGCFT1_send_done&) {
	return false;
}

