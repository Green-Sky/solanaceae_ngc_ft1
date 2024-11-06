#include "./ngc_hs2_send.hpp"

#include <solanaceae/util/span.hpp>

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <solanaceae/contact/components.hpp>

// https://www.youtube.com/watch?v=AdAqsgga3qo

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

	// work request queue
	// check if already running, discard

	return 1000.f;
}

void NGCHS2Send::handleRange(Contact3Handle c, const Events::NGCFT1_recv_request& e) {
	ByteSpan fid{e.file_id, e.file_id_size};
	// parse
	// - ts start
	// - ts end

	// dedupe insert into queue
}

void NGCHS2Send::handleSingleMessage(Contact3Handle c, const Events::NGCFT1_recv_request& e) {
	ByteSpan fid{e.file_id, e.file_id_size};
	if (fid.size != 32+sizeof(uint32_t)+sizeof(uint64_t)) {
		// error
		return;
	}

	// parse
	// - ppk
	// TOX_GROUP_PEER_PUBLIC_KEY_SIZE (32)
	ByteSpan ppk{fid.ptr, 32};

	// - mid
	//static_assert(sizeof(Tox_Group_Message_Id) == sizeof(uint32_t));
	ByteSpan mid_bytes{fid.ptr+ppk.size, sizeof(uint32_t)};

	// - ts
	// uint64_t (seconds? we dont want milliseconds
	ByteSpan ts_bytes{mid_bytes.ptr+mid_bytes.size, sizeof(uint64_t)};

	// file content
	// - message type (text/textaction/file(ft1sha1))
	// - if text/textaction
	//   - text (string)
	// - else if file
	//   - file type
	//   - file id

	// for queue, we need group, peer, msg_ppk, msg_mid, msg_ts

	// dedupe insert into queue
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
		e.file_kind != NGCFT1_file_kind::HS2_INFO_RANGE_TIME &&
		e.file_kind != NGCFT1_file_kind::HS2_SINGLE_MESSAGE
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

	if (e.file_kind == NGCFT1_file_kind::HS2_INFO_RANGE_TIME) {
		handleRange(c, e);
	} else if (e.file_kind == NGCFT1_file_kind::HS2_SINGLE_MESSAGE) {
		handleSingleMessage(c, e);
	}

	return true;
}

bool NGCHS2Send::onEvent(const Events::NGCFT1_send_data&) {
	return false;
}

bool NGCHS2Send::onEvent(const Events::NGCFT1_send_done&) {
	return false;
}

