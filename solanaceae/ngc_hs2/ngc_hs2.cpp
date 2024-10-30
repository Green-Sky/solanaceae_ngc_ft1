#include "./ngc_hs2.hpp"

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

NGCHS2::NGCHS2(
	ToxContactModel2& tcm,
	ToxEventProviderI& tep,
	NGCFT1& nft
) :
	_tcm(tcm),
	_tep_sr(tep.newSubRef(this)),
	_nft(nft),
	_nftep_sr(_nft.newSubRef(this))
{
	_tep_sr
		.subscribe(TOX_EVENT_GROUP_PEER_JOIN)
		.subscribe(TOX_EVENT_GROUP_PEER_EXIT)
	;

	_nftep_sr
		.subscribe(NGCFT1_Event::recv_init)
		.subscribe(NGCFT1_Event::recv_request)
		.subscribe(NGCFT1_Event::recv_init)
		.subscribe(NGCFT1_Event::recv_data)
		.subscribe(NGCFT1_Event::send_data)
		.subscribe(NGCFT1_Event::recv_done)
		.subscribe(NGCFT1_Event::send_done)
	;
}

NGCHS2::~NGCHS2(void) {
}

float NGCHS2::iterate(float delta) {
	return 1000.f;
}

bool NGCHS2::onEvent(const Events::NGCFT1_recv_request& e) {
	if (
		e.file_kind != NGCFT1_file_kind::HS2_INFO_RANGE_TIME &&
		e.file_kind != NGCFT1_file_kind::HS2_SINGLE_MESSAGE
	) {
		return false; // not for us
	}

	return false;
}

bool NGCHS2::onEvent(const Events::NGCFT1_recv_init& e) {
	if (
		e.file_kind != NGCFT1_file_kind::HS2_INFO_RANGE_TIME &&
		e.file_kind != NGCFT1_file_kind::HS2_SINGLE_MESSAGE
	) {
		return false; // not for us
	}

	return false;
}

bool NGCHS2::onEvent(const Events::NGCFT1_recv_data&) {
	return false;
}

bool NGCHS2::onEvent(const Events::NGCFT1_send_data&) {
	return false;
}

bool NGCHS2::onEvent(const Events::NGCFT1_recv_done&) {
	return false;
}

bool NGCHS2::onEvent(const Events::NGCFT1_send_done&) {
	return false;
}

bool NGCHS2::onToxEvent(const Tox_Event_Group_Peer_Join* e) {
	const auto group_number = tox_event_group_peer_join_get_group_number(e);
	const auto peer_number = tox_event_group_peer_join_get_peer_id(e);

	const auto c = _tcm.getContactGroupPeer(group_number, peer_number);
	assert(c);

	// add to check list with inital cooldown

	return false;
}

bool NGCHS2::onToxEvent(const Tox_Event_Group_Peer_Exit* e) {
	return false;
}

