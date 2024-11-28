#include "./ngc_hs2_send.hpp"

#include <solanaceae/util/span.hpp>

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <solanaceae/contact/components.hpp>

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
		//.subscribe(NGCFT1_Event::recv_init) // we only send init
		//.subscribe(NGCFT1_Event::recv_data) // we only send data
		.subscribe(NGCFT1_Event::send_data)
		//.subscribe(NGCFT1_Event::recv_done)
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

