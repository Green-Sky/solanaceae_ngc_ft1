#include "./ngcext.hpp"

#include <iostream>
#include <cassert>

NGCEXTEventProvider::NGCEXTEventProvider(ToxI& t, ToxEventProviderI& tep) : _t(t), _tep(tep), _tep_sr(_tep.newSubRef(this)) {
	_tep_sr
		.subscribe(Tox_Event_Type::TOX_EVENT_GROUP_CUSTOM_PACKET)
		.subscribe(Tox_Event_Type::TOX_EVENT_GROUP_CUSTOM_PRIVATE_PACKET)
	;
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
		e.file_size |= uint64_t(data[curser]) << (i*8);
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

bool NGCEXTEventProvider::parse_ft1_init_ack_v3(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_init_ack_v3 cant be public\n";
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

	// - 1 byte (feature_flags)
	if ((data_size - curser) >= sizeof(e.feature_flags)) {
		e.feature_flags = data[curser++];
	} else {
		e.feature_flags = 0x00; // default
	}

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

	e.sequence_ids.reserve(std::max<int64_t>(data_size-curser, 1)/sizeof(uint16_t));
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

bool NGCEXTEventProvider::parse_ft1_have(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_have cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_have e;
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
	uint16_t file_id_size = 0u;
	_DATA_HAVE(sizeof(file_id_size), std::cerr << "NGCEXT: packet too small, missing file_id_size\n"; return false)
	for (size_t i = 0; i < sizeof(file_id_size); i++, curser++) {
		file_id_size |= uint32_t(data[curser]) << (i*8);
	}

	_DATA_HAVE(file_id_size, std::cerr << "NGCEXT: packet too small, missing file_id, or file_id_size too large(" << data_size-curser << ")\n"; return false)

	e.file_id = {data+curser, data+curser+file_id_size};
	curser += file_id_size;

	// - array [
	//   - 4 bytes (chunk index)
	// - ]
	while (curser < data_size) {
		_DATA_HAVE(sizeof(uint32_t), std::cerr << "NGCEXT: packet too small, broken chunk index\n"; return false)
		uint32_t chunk_index = 0u;
		for (size_t i = 0; i < sizeof(chunk_index); i++, curser++) {
			chunk_index |= uint32_t(data[curser]) << (i*8);
		}
		e.chunks.push_back(chunk_index);
	}

	return dispatch(
		NGCEXT_Event::FT1_HAVE,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_bitset(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_bitset cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_bitset e;
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
	uint16_t file_id_size = 0u;
	_DATA_HAVE(sizeof(file_id_size), std::cerr << "NGCEXT: packet too small, missing file_id_size\n"; return false)
	for (size_t i = 0; i < sizeof(file_id_size); i++, curser++) {
		file_id_size |= uint32_t(data[curser]) << (i*8);
	}

	_DATA_HAVE(file_id_size, std::cerr << "NGCEXT: packet too small, missing file_id, or file_id_size too large (" << data_size-curser << ")\n"; return false)

	e.file_id = {data+curser, data+curser+file_id_size};
	curser += file_id_size;

	e.start_chunk = 0u;
	_DATA_HAVE(sizeof(e.start_chunk), std::cerr << "NGCEXT: packet too small, missing start_chunk\n"; return false)
	for (size_t i = 0; i < sizeof(e.start_chunk); i++, curser++) {
		e.start_chunk |= uint32_t(data[curser]) << (i*8);
	}

	// - X bytes
	// - array [
	//   - 1 bit (have chunk)
	// - ] (filled up with zero)
	// high to low?
	// simply rest of file packet
	e.chunk_bitset = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_BITSET,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_have_all(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	// can be public
	// TODO: warn on public?

	Events::NGCEXT_ft1_have_all e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - 4 byte (file_kind)
	e.file_kind = 0u;
	_DATA_HAVE(sizeof(e.file_kind), std::cerr << "NGCEXT: packet too small, missing file_kind\n"; return false)
	for (size_t i = 0; i < sizeof(e.file_kind); i++, curser++) {
		e.file_kind |= uint32_t(data[curser]) << (i*8);
	}

	_DATA_HAVE(1, std::cerr << "NGCEXT: packet too small, missing file_id\n"; return false)

	// - X bytes (file_id, differnt sizes)
	e.file_id = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_HAVE_ALL,
		e
	);
}

bool NGCEXTEventProvider::parse_ft1_init2(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	if (!_private) {
		std::cerr << "NGCEXT: ft1_init2 cant be public\n";
		return false;
	}

	Events::NGCEXT_ft1_init2 e;
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
		e.file_size |= uint64_t(data[curser]) << (i*8);
	}

	// - 1 byte (temporary_file_tf_id)
	_DATA_HAVE(sizeof(e.transfer_id), std::cerr << "NGCEXT: packet too small, missing transfer_id\n"; return false)
	e.transfer_id = data[curser++];

	// - 1 byte feature flags
	_DATA_HAVE(sizeof(e.feature_flags), std::cerr << "NGCEXT: packet too small, missing feature_flags\n"; return false)
	e.feature_flags = data[curser++];

	// - X bytes (file_kind dependent id, differnt sizes)
	e.file_id = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::FT1_INIT2,
		e
	);
}

bool NGCEXTEventProvider::parse_pc1_announce(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* data, size_t data_size,
	bool _private
) {
	// can be public
	Events::NGCEXT_pc1_announce e;
	e.group_number = group_number;
	e.peer_number = peer_number;
	size_t curser = 0;

	// - X bytes (id, differnt sizes)
	e.id = {data+curser, data+curser+(data_size-curser)};

	return dispatch(
		NGCEXT_Event::PC1_ANNOUNCE,
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
			//return parse_ft1_init_ack_v2(group_number, peer_number, data+1, data_size-1, _private);
			return parse_ft1_init_ack_v3(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_DATA:
			return parse_ft1_data(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_DATA_ACK:
			return parse_ft1_data_ack(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_MESSAGE:
			return parse_ft1_message(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_HAVE:
			return parse_ft1_have(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_BITSET:
			return parse_ft1_bitset(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_HAVE_ALL:
			return parse_ft1_have_all(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::FT1_INIT2:
			return parse_ft1_init2(group_number, peer_number, data+1, data_size-1, _private);
		case NGCEXT_Event::PC1_ANNOUNCE:
			return parse_pc1_announce(group_number, peer_number, data+1, data_size-1, _private);
		default:
			return false;
	}

	return false;
}

bool NGCEXTEventProvider::send_ft1_request(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size
) {
	// - 1 byte packet id
	// - 4 byte file_kind
	// - X bytes file_id
	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_REQUEST));
	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_init(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	uint64_t file_size,
	uint8_t transfer_id,
	const uint8_t* file_id, size_t file_id_size
) {
	// - 1 byte packet id
	// - 4 byte (file_kind)
	// - 8 bytes (data size)
	// - 1 byte (temporary_file_tf_id, for this peer only, technically just a prefix to distinguish between simultainious fts)
	// - X bytes (file_kind dependent id, differnt sizes)

	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_INIT));
	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < sizeof(file_size); i++) {
		pkg.push_back((file_size>>(i*8)) & 0xff);
	}
	pkg.push_back(transfer_id);
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_init_ack(
	uint32_t group_number, uint32_t peer_number,
	uint8_t transfer_id
) {
	// - 1 byte packet id
	// - 1 byte transfer_id
	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_INIT_ACK));
	pkg.push_back(transfer_id);

	// - 2 bytes max_lossy_data_size
	const uint16_t max_lossy_data_size = _t.toxGroupMaxCustomLossyPacketLength() - 4;
	for (size_t i = 0; i < sizeof(uint16_t); i++) {
		pkg.push_back((max_lossy_data_size>>(i*8)) & 0xff);
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_data(
	uint32_t group_number, uint32_t peer_number,
	uint8_t transfer_id,
	uint16_t sequence_id,
	const uint8_t* data, size_t data_size
) {
	assert(data_size > 0);

	// TODO
	// check header_size+data_size <= max pkg size

	std::vector<uint8_t> pkg;
	pkg.reserve(2048); // saves a ton of allocations
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_DATA));
	pkg.push_back(transfer_id);
	pkg.push_back(sequence_id & 0xff);
	pkg.push_back((sequence_id >> (1*8)) & 0xff);

	// TODO: optimize
	for (size_t i = 0; i < data_size; i++) {
		pkg.push_back(data[i]);
	}

	// lossy
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, false, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_data_ack(
	uint32_t group_number, uint32_t peer_number,
	uint8_t transfer_id,
	const uint16_t* seq_ids, size_t seq_ids_size
) {
	std::vector<uint8_t> pkg;
	pkg.reserve(1+1+2*32); // 32acks in a single pkg should be unlikely
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_DATA_ACK));
	pkg.push_back(transfer_id);

	// TODO: optimize
	for (size_t i = 0; i < seq_ids_size; i++) {
		pkg.push_back(seq_ids[i] & 0xff);
		pkg.push_back((seq_ids[i] >> (1*8)) & 0xff);
	}

	// lossy
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, false, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_all_ft1_message(
	uint32_t group_number,
	uint32_t message_id,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size
) {
	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_MESSAGE));

	for (size_t i = 0; i < sizeof(message_id); i++) {
		pkg.push_back((message_id>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	// lossless
	return _t.toxGroupSendCustomPacket(group_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_have(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size,
	const uint32_t* chunks_data, size_t chunks_size
) {
	// 16bit file id size
	assert(file_id_size <= 0xffff);
	if (file_id_size > 0xffff) {
		return false;
	}

	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_HAVE));

	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}

	// file id not last in packet, needs explicit size
	const uint16_t file_id_size_cast = file_id_size;
	for (size_t i = 0; i < sizeof(file_id_size_cast); i++) {
		pkg.push_back((file_id_size_cast>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	// rest is chunks
	for (size_t c_i = 0; c_i < chunks_size; c_i++) {
		for (size_t i = 0; i < sizeof(chunks_data[c_i]); i++) {
			pkg.push_back((chunks_data[c_i]>>(i*8)) & 0xff);
		}
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_bitset(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size,
	uint32_t start_chunk,
	const uint8_t* bitset_data, size_t bitset_size // size is bytes
) {
	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_BITSET));

	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}

	// file id not last in packet, needs explicit size
	const uint16_t file_id_size_cast = file_id_size;
	for (size_t i = 0; i < sizeof(file_id_size_cast); i++) {
		pkg.push_back((file_id_size_cast>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	for (size_t i = 0; i < sizeof(start_chunk); i++) {
		pkg.push_back((start_chunk>>(i*8)) & 0xff);
	}

	for (size_t i = 0; i < bitset_size; i++) {
		pkg.push_back(bitset_data[i]);
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_have_all(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size
) {
	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_HAVE_ALL));

	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_ft1_init2(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	uint64_t file_size,
	uint8_t transfer_id,
	uint8_t feature_flags,
	const uint8_t* file_id, size_t file_id_size
) {
	// - 1 byte packet id
	// - 4 byte (file_kind)
	// - 8 bytes (data size)
	// - 1 byte (temporary_file_tf_id, for this peer only, technically just a prefix to distinguish between simultainious fts)
	// - 1 byte (feature_flags)
	// - X bytes (file_kind dependent id, differnt sizes)

	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_INIT2));
	for (size_t i = 0; i < sizeof(file_kind); i++) {
		pkg.push_back((file_kind>>(i*8)) & 0xff);
	}
	for (size_t i = 0; i < sizeof(file_size); i++) {
		pkg.push_back((file_size>>(i*8)) & 0xff);
	}
	pkg.push_back(transfer_id);
	pkg.push_back(feature_flags);
	for (size_t i = 0; i < file_id_size; i++) {
		pkg.push_back(file_id[i]);
	}

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

static std::vector<uint8_t> build_pc1_announce(const uint8_t* id_data, size_t id_size) {
	// - 1 byte packet id
	// - X bytes (id, differnt sizes)

	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::PC1_ANNOUNCE));
	for (size_t i = 0; i < id_size; i++) {
		pkg.push_back(id_data[i]);
	}
	return pkg;
}

bool NGCEXTEventProvider::send_pc1_announce(
	uint32_t group_number, uint32_t peer_number,
	const uint8_t* id_data, size_t id_size
) {
	auto pkg = build_pc1_announce(id_data, id_size);

	std::cout << "NEEP: sending PC1_ANNOUNCE s:" << pkg.size() - sizeof(NGCEXT_Event::PC1_ANNOUNCE) << "\n";

	// lossless?
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCEXTEventProvider::send_all_pc1_announce(
	uint32_t group_number,
	const uint8_t* id_data, size_t id_size
) {
	auto pkg = build_pc1_announce(id_data, id_size);

	std::cout << "NEEP: sending all PC1_ANNOUNCE s:" << pkg.size() - sizeof(NGCEXT_Event::PC1_ANNOUNCE) << "\n";

	// lossless?
	return _t.toxGroupSendCustomPacket(group_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PACKET_OK;
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

