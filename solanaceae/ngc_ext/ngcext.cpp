#include "./ngcext.hpp"

#include <iostream>

NGCEXTEventProvider::NGCEXTEventProvider(ToxEventProviderI& tep) : _tep(tep) {
	_tep.subscribe(this, Tox_Event_Type::TOX_EVENT_GROUP_CUSTOM_PACKET);
	_tep.subscribe(this, Tox_Event_Type::TOX_EVENT_GROUP_CUSTOM_PRIVATE_PACKET);
}

#define _DATA_HAVE(x, error) if ((data_size - curser) < (x)) { error; }

bool NGCEXTEventProvider::parse_hs1_request_last_ids(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	return false;
}

bool NGCEXTEventProvider::parse_hs1_response_last_ids(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	return false;
}

bool NGCEXTEventProvider::parse_ft1_request(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool // dont care private
) {
	Events::NGCEXT_ft1_request e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 4 byte (file_kind)
	e.file_kind = 0u;
	_DATA_HAVE(sizeof(e.file_kind), std::cerr << "NGCEXT: packet too small, missing file_kind\n"; return false)
	for (size_t i = 0; i < sizeof(e.file_kind); i++, curser++) {
		e.file_kind |= uint32_t(data[curser]) << (i*8);
	}

	// - X bytes (file_kind dependent id, differnt sizes)
	e.file_id = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_REQUEST,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_init(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_init cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_init e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 4 byte (file_kind)
	e.file_kind = 0u;
	_DATA_HAVE(sizeof(e.file_kind), std::cerr << "NGCEXT: packet too small, missing file_kind\n"; return false)
	for (size_t i = 0; i < sizeof(e.file_kind); i++, curser++) {
		e.file_kind |= uint32_t(data[curser]) << (i*8);
	}

	// - 8 bytes (data size)
	e.file_size = 0u;
	_DATA_HAVE(sizeof(e.file_size), std::cerr << "NGCEXT: packet too small, missing file_size\n"; return false)
	for (size_t i = 0; i < sizeof(e.file_size); i++, curser++) {
		e.file_size |= size_t(data[curser]) << (i*8);
	}

	// - 1 byte (temporary_file_tf_id)
	_DATA_HAVE(sizeof(e.transfer_id), std::cerr << "NGCEXT: packet too small, missing transfer_id\n"; return false)
	e.transfer_id = data[curser++];

	// - X bytes (file_kind dependent id, differnt sizes)
	e.file_id = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_INIT,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_init_ack(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_init_ack cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_init_ack e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 1 byte (temporary_file_tf_id)
	_DATA_HAVE(sizeof(e.transfer_id), std::cerr << "NGCEXT: packet too small, missing transfer_id\n"; return false)
	e.transfer_id = data[curser++];

	e.max_lossy_data_size = 500-4; // -4 and 500 are hardcoded

	return dispatch(
		NGCEXT_Event::FT1_INIT_ACK,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_data(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_data cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_data e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 1 byte (temporary_file_tf_id)
	_DATA_HAVE(sizeof(e.transfer_id), std::cerr << "NGCEXT: packet too small, missing transfer_id\n"; return false)
	e.transfer_id = data[curser++];

	// - 2 bytes (sequence_id)
	e.sequence_id = 0u;
	_DATA_HAVE(sizeof(e.sequence_id), std::cerr << "NGCEXT: packet too small, missing sequence_id\n"; return false)
	for (size_t i = 0; i < sizeof(e.sequence_id); i++, curser++) {
		e.sequence_id |= uint32_t(data[curser]) << (i*8);
	}

	// - X bytes (the data fragment)
	// (size is implicit)
	e.data = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_DATA,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_data_ack(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_data_ack cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_data_ack e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 1 byte (temporary_file_tf_id)
	_DATA_HAVE(sizeof(e.transfer_id), std::cerr << "NGCEXT: packet too small, missing transfer_id\n"; return false)
	e.transfer_id = data[curser++];

	while (curser < data_size) {
		_DATA_HAVE(sizeof(uint16_t), std::cerr << "NGCEXT: packet too small, missing seq_id\n"; return false)
		uint16_t seq_id = data[curser++];
		seq_id |= data[curser++] << (1*8);
		e.sequence_ids.push_back(seq_id);
	}

	return dispatch(
		NGCEXT_Event::FT1_DATA_ACK,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_message(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (_private) {
		std::cerr << "NGCEXT: ft1_message cant be private (yet)\n";
		return false;
	}

	Events::NGCEXT_ft1_message e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 4 byte (message_id)
	e.message_id = 0u;
	_DATA_HAVE(sizeof(e.message_id), std::cerr << "NGCEXT: packet too small, missing message_id\n"; return false)
	for (size_t i = 0; i < sizeof(e.message_id); i++, curser++) {
		e.message_id |= uint32_t(data[curser]) << (i*8);
	}

	// - 4 byte (file_kind)
	e.file_kind = 0u;
	_DATA_HAVE(sizeof(e.file_kind), std::cerr << "NGCEXT: packet too small, missing file_kind\n"; return false)
	for (size_t i = 0; i < sizeof(e.file_kind); i++, curser++) {
		e.file_kind |= uint32_t(data[curser]) << (i*8);
	}

	// - X bytes (file_kind dependent id, differnt sizes)
	e.file_id = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_MESSAGE,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_init_ack_v2(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_init_ack_v2 cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_init_ack e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 1 byte (temporary_file_tf_id)
	_DATA_HAVE(sizeof(e.transfer_id), std::cerr << "NGCEXT: packet too small, missing transfer_id\n"; return false)
	e.transfer_id = data[curser++];

	// - 2 byte (max_lossy_data_size)
	if ((data_size - curser) >= sizeof(e.max_lossy_data_size)) {
		e.max_lossy_data_size = 0;
		for (size_t i = 0; i < sizeof(e.max_lossy_data_size); i++, curser++) {
			e.max_lossy_data_size |= uint16_t(data[curser]) << (i*8);
		}
	} else {
		e.max_lossy_data_size = 500-4; // default
	}

	return dispatch(
		NGCEXT_Event::FT1_INIT_ACK,
		e
	);
}

bool NGCEXTEventProvider::handlePacket(
	const uint32_t group_number,
	const uint32_t peer_number,
	const uint8_t* data,
	const size_t data_size,
	const bool _private
) {
	if (data_size < 1) {
		return false; // waht
	}

	NGCEXT_Event pkg_type = static_cast<NGCEXT_Event>(data[0]);

	switch (pkg_type) {
		case NGCEXT_Event::HS1_REQUEST_LAST_IDS:
			return false;
		case NGCEXT_Event::HS1_RESPONSE_LAST_IDS:
			return false;
		case NGCEXT_Event::FT1_REQUEST:
			return parse_ft1_request(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_INIT:
			return parse_ft1_init(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_INIT_ACK:
			//return parse_ft1_init_ack(group_number, peer_number, data+1, data_size-1, _private);
			return parse_ft1_init_ack_v2(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_DATA:
			return parse_ft1_data(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_DATA_ACK:
			return parse_ft1_data_ack(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_MESSAGE:
			return parse_ft1_message(group_number, peer_number, data+1, data_size-1, _private);
		default:
			return false;
	}

	return false;
}

bool NGCEXTEventProvider::onToxEvent(const Tox_Event_Group_Custom_Packet* e) {
	const auto group_number = tox_event_group_custom_packet_get_group_number(e);
	const auto peer_number = tox_event_group_custom_packet_get_peer_id(e);
	const uint8_t* data = tox_event_group_custom_packet_get_data(e);
	const auto data_length = tox_event_group_custom_packet_get_data_length(e);

	return handlePacket(group_number, peer_number, data, data_length, false);
}

bool NGCEXTEventProvider::onToxEvent(const Tox_Event_Group_Custom_Private_Packet* e) {
	const auto group_number = tox_event_group_custom_private_packet_get_group_number(e);
	const auto peer_number = tox_event_group_custom_private_packet_get_peer_id(e);
	const uint8_t* data = tox_event_group_custom_private_packet_get_data(e);
	const auto data_length = tox_event_group_custom_private_packet_get_data_length(e);

	return handlePacket(group_number, peer_number, data, data_length, true);
}

