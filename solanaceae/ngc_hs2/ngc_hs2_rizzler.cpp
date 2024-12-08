#include "./ngc_hs2_rizzler.hpp"

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/ngc_ft1/ngcft1_file_kind.hpp>

#include <solanaceae/util/span.hpp>

#include <iostream>

NGCHS2Rizzler::NGCHS2Rizzler(
	Contact3Registry& cr,
	RegistryMessageModelI& rmm,
	ToxContactModel2& tcm,
	NGCFT1& nft,
	ToxEventProviderI& tep
) :
	_cr(cr),
	_rmm(rmm),
	_tcm(tcm),
	_nft(nft),
	_nftep_sr(_nft.newSubRef(this)),
	_tep_sr(tep.newSubRef(this))

{
	_nftep_sr
		.subscribe(NGCFT1_Event::recv_init)
		.subscribe(NGCFT1_Event::recv_data)
		.subscribe(NGCFT1_Event::recv_done)
	;
	_tep_sr
		.subscribe(Tox_Event_Type::TOX_EVENT_GROUP_PEER_JOIN)
	;
}

NGCHS2Rizzler::~NGCHS2Rizzler(void) {
}

float NGCHS2Rizzler::iterate(float delta) {
	for (auto it = _request_queue.begin(); it != _request_queue.end();) {
		it->second.timer += delta;

		if (it->second.timer < it->second.delay) {
			it++;
			continue;
		}

		if (!_cr.all_of<Contact::Components::ToxGroupPeerEphemeral>(it->first)) {
			// peer nolonger online
			it = _request_queue.erase(it);
			continue;
		}

		const auto [group_number, peer_number] = _cr.get<Contact::Components::ToxGroupPeerEphemeral>(it->first);

		// now in sec
		const uint64_t ts_now = Message::getTimeMS()/1000;

		if (sendRequest(group_number, peer_number, ts_now, ts_now-(60*60*48))) {
			// TODO: requeue
			// TODO: segment
			// TODO: dont request already received ranges

			//// on success, requeue with longer delay (minutes)

			//it->second.timer = 0.f;
			//it->second.delay = _delay_next_request_min + _rng_dist(_rng)*_delay_next_request_add;

			//// double the delay for overlap (9m-15m)
			//// TODO: finetune
			//it->second.sync_delta = uint8_t((it->second.delay/60.f)*2.f) + 1;

			//std::cout << "ZOX #### requeued request in " << it->second.delay << "s\n";

		} else {
			// on failure, assume disconnected
		}

		// remove from request queue
		it = _request_queue.erase(it);
	}

	return 1000.f;
}

template<typename Type>
static uint64_t deserlSimpleType(ByteSpan bytes) {
	if (bytes.size < sizeof(Type)) {
		throw int(1);
	}

	Type value{};

	for (size_t i = 0; i < sizeof(Type); i++) {
		value |= Type(bytes[i]) << (i*8);
	}

	return value;
}

static uint64_t deserlTS(ByteSpan ts_bytes) {
	return deserlSimpleType<uint64_t>(ts_bytes);
}

template<typename Type>
static void serlSimpleType(std::vector<uint8_t>& bytes, const Type& value) {
	for (size_t i = 0; i < sizeof(Type); i++) {
		bytes.push_back(uint8_t(value >> (i*8) & 0xff));
	}
}

bool NGCHS2Rizzler::sendRequest(
	uint32_t group_number, uint32_t peer_number,
	uint64_t ts_start, uint64_t ts_end
) {
	std::cout << "NGCHS2Rizzler: sending request to " << group_number << ":" << peer_number << " (" << ts_start << "," << ts_end << ")\n";

	// build fid
	std::vector<uint8_t> fid;
	fid.reserve(sizeof(uint64_t)+sizeof(uint64_t));

	serlSimpleType(fid, ts_start);
	serlSimpleType(fid, ts_end);

	assert(fid.size() == sizeof(uint64_t)+sizeof(uint64_t));

	return _nft.NGC_FT1_send_request_private(
		group_number, peer_number,
		(uint32_t)NGCFT1_file_kind::HS2_RANGE_TIME_MSGPACK,
		fid.data(), fid.size() // fid
	);
}

bool NGCHS2Rizzler::onEvent(const Events::NGCFT1_recv_init& e) {
	std::cout << "NGCHS2Rizzler: recv_init " << e.group_number << ":" << e.peer_number << "." << (int)e.transfer_id << "\n";
	return false;
}

bool NGCHS2Rizzler::onEvent(const Events::NGCFT1_recv_data&) {
	return false;
}

bool NGCHS2Rizzler::onEvent(const Events::NGCFT1_recv_done&) {
	return false;
}

bool NGCHS2Rizzler::onToxEvent(const Tox_Event_Group_Peer_Join* e) {
	const auto group_number = tox_event_group_peer_join_get_group_number(e);
	const auto peer_number = tox_event_group_peer_join_get_peer_id(e);

	const auto c = _tcm.getContactGroupPeer(group_number, peer_number);

	if (!c) {
		return false;
	}

	if (!_request_queue.count(c)) {
		_request_queue[c] = {
			_delay_before_first_request_min + _rng_dist(_rng)*_delay_before_first_request_add,
			0.f,
			0,
		};
	}

	return false;
}

