#include "./ngcft1.hpp"

#include <solanaceae/toxcore/utils.hpp>

#include <sodium.h>

#include <iostream>
#include <set>
#include <algorithm>
#include <cassert>
#include <vector>

bool NGCFT1::sendPKG_FT1_REQUEST(
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

bool NGCFT1::sendPKG_FT1_INIT(
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

bool NGCFT1::sendPKG_FT1_INIT_ACK(
	uint32_t group_number, uint32_t peer_number,
	uint8_t transfer_id
) {
	// - 1 byte packet id
	// - 1 byte transfer_id
	std::vector<uint8_t> pkg;
	pkg.push_back(static_cast<uint8_t>(NGCEXT_Event::FT1_INIT_ACK));
	pkg.push_back(transfer_id);

	// lossless
	return _t.toxGroupSendCustomPrivatePacket(group_number, peer_number, true, pkg) == TOX_ERR_GROUP_SEND_CUSTOM_PRIVATE_PACKET_OK;
}

bool NGCFT1::sendPKG_FT1_DATA(
	uint32_t group_number, uint32_t peer_number,
	uint8_t transfer_id,
	uint16_t sequence_id,
	const uint8_t* data, size_t data_size
) {
	assert(data_size > 0);

	// TODO
	// check header_size+data_size <= max pkg size

	std::vector<uint8_t> pkg;
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

bool NGCFT1::sendPKG_FT1_DATA_ACK(
	uint32_t group_number, uint32_t peer_number,
	uint8_t transfer_id,
	const uint16_t* seq_ids, size_t seq_ids_size
) {
	std::vector<uint8_t> pkg;
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

bool NGCFT1::sendPKG_FT1_MESSAGE(
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

void NGCFT1::updateSendTransfer(float time_delta, uint32_t group_number, uint32_t peer_number, Group::Peer& peer, size_t idx, std::set<CCAI::SeqIDType>& timeouts_set) {
	auto& tf_opt = peer.send_transfers.at(idx);
	assert(tf_opt.has_value());
	auto& tf = tf_opt.value();

	tf.time_since_activity += time_delta;

	switch (tf.state) {
		using State = Group::Peer::SendTransfer::State;
		case State::INIT_SENT:
			if (tf.time_since_activity >= init_retry_timeout_after) {
				if (tf.inits_sent >= 3) {
					// delete, timed out 3 times
					std::cerr << "NGCFT1 warning: ft init timed out, deleting\n";
					dispatch(
						NGCFT1_Event::send_done,
						Events::NGCFT1_send_done{
							group_number, peer_number,
							static_cast<uint8_t>(idx),
						}
					);
					tf_opt.reset();
				} else {
					// timed out, resend
					std::cerr << "NGCFT1 warning: ft init timed out, resending\n";
					sendPKG_FT1_INIT(group_number, peer_number, tf.file_kind, tf.file_size, idx, tf.file_id.data(), tf.file_id.size());
					tf.inits_sent++;
					tf.time_since_activity = 0.f;
				}
			}
			//break;
			return;
		case State::SENDING: {
				tf.ssb.for_each(time_delta, [&](uint16_t id, const std::vector<uint8_t>& data, float& time_since_activity) {
					// no ack after 5 sec -> resend
					//if (time_since_activity >= ngc_ft1_ctx->options.sending_resend_without_ack_after) {
					if (timeouts_set.count({idx, id})) {
						// TODO: can fail
						sendPKG_FT1_DATA(group_number, peer_number, idx, id, data.data(), data.size());
						peer.cca->onLoss({idx, id}, false);
						time_since_activity = 0.f;
						timeouts_set.erase({idx, id});
					}
				});

				if (tf.time_since_activity >= sending_give_up_after) {
					// no ack after 30sec, close ft
					std::cerr << "NGCFT1 warning: sending ft in progress timed out, deleting\n";
					dispatch(
						NGCFT1_Event::send_done,
						Events::NGCFT1_send_done{
							group_number, peer_number,
							static_cast<uint8_t>(idx),
						}
					);

					// clean up cca
					tf.ssb.for_each(time_delta, [&](uint16_t id, const std::vector<uint8_t>& data, float& time_since_activity) {
						peer.cca->onLoss({idx, id}, true);
						timeouts_set.erase({idx, id});
					});

					tf_opt.reset();
					//continue; // dangerous control flow
					return;
				}

				// if chunks in flight < window size (2)
				//while (tf.ssb.size() < ngc_ft1_ctx->options.packet_window_size) {
				int64_t can_packet_size {static_cast<int64_t>(peer.cca->canSend())};
				//if (can_packet_size) {
					//std::cerr << "FT: can_packet_size: " << can_packet_size;
				//}
				size_t count {0};
				while (can_packet_size > 0 && tf.file_size > 0) {
					std::vector<uint8_t> new_data;

					// TODO: parameterize packet size? -> only if JF increases lossy packet size >:)
					//size_t chunk_size = std::min<size_t>(496u, tf.file_size - tf.file_size_current);
					//size_t chunk_size = std::min<size_t>(can_packet_size, tf.file_size - tf.file_size_current);
					size_t chunk_size = std::min<size_t>({
						//496u,
						//996u,
						peer.cca->MAXIMUM_SEGMENT_DATA_SIZE,
						static_cast<size_t>(can_packet_size),
						tf.file_size - tf.file_size_current
					});
					if (chunk_size == 0) {
						tf.state = State::FINISHING;
						break; // we done
					}

					new_data.resize(chunk_size);

					//ngc_ft1_ctx->cb_send_data[tf.file_kind](
						//tox,
						//group_number, peer_number,
						//idx,
						//tf.file_size_current,
						//new_data.data(), new_data.size(),
						//ngc_ft1_ctx->ud_send_data.count(tf.file_kind) ? ngc_ft1_ctx->ud_send_data.at(tf.file_kind) : nullptr
					//);
					assert(idx <= 0xffu);
					// TODO: check return value
					dispatch(
						NGCFT1_Event::send_data,
						Events::NGCFT1_send_data{
							group_number, peer_number,
							static_cast<uint8_t>(idx),
							tf.file_size_current,
							new_data.data(), new_data.size(),
						}
					);

					uint16_t seq_id = tf.ssb.add(std::move(new_data));
					sendPKG_FT1_DATA(group_number, peer_number, idx, seq_id, tf.ssb.entries.at(seq_id).data.data(), tf.ssb.entries.at(seq_id).data.size());
					peer.cca->onSent({idx, seq_id}, chunk_size);

#if defined(EXTRA_LOGGING) && EXTRA_LOGGING == 1
					fprintf(stderr, "FT: sent data size: %ld (seq %d)\n", chunk_size, seq_id);
#endif

					tf.file_size_current += chunk_size;
					can_packet_size -= chunk_size;
					count++;
				}
				//if (count) {
					//std::cerr << " split over " << count << "\n";
				//}
			}
			break;
		case State::FINISHING: // we still have unacked packets
			tf.ssb.for_each(time_delta, [&](uint16_t id, const std::vector<uint8_t>& data, float& time_since_activity) {
				// no ack after 5 sec -> resend
				//if (time_since_activity >= ngc_ft1_ctx->options.sending_resend_without_ack_after) {
				if (timeouts_set.count({idx, id})) {
					sendPKG_FT1_DATA(group_number, peer_number, idx, id, data.data(), data.size());
					peer.cca->onLoss({idx, id}, false);
					time_since_activity = 0.f;
					timeouts_set.erase({idx, id});
				}
			});
			if (tf.time_since_activity >= sending_give_up_after) {
				// no ack after 30sec, close ft
				// TODO: notify app
				std::cerr << "NGCFT1 warning: sending ft finishing timed out, deleting\n";

				// clean up cca
				tf.ssb.for_each(time_delta, [&](uint16_t id, const std::vector<uint8_t>& data, float& time_since_activity) {
					peer.cca->onLoss({idx, id}, true);
					timeouts_set.erase({idx, id});
				});

				tf_opt.reset();
			}
			break;
		default: // invalid state, delete
			std::cerr << "NGCFT1 error: ft in invalid state, deleting\n";
			tf_opt.reset();
			//continue;
			return;
	}
}

void NGCFT1::iteratePeer(float time_delta, uint32_t group_number, uint32_t peer_number, Group::Peer& peer) {
	auto timeouts = peer.cca->getTimeouts();
	std::set<CCAI::SeqIDType> timeouts_set{timeouts.cbegin(), timeouts.cend()};

	for (size_t idx = 0; idx < peer.send_transfers.size(); idx++) {
		if (peer.send_transfers.at(idx).has_value()) {
			updateSendTransfer(time_delta, group_number, peer_number, peer, idx, timeouts_set);
		}
	}

	// TODO: receiving tranfers?
}

NGCFT1::NGCFT1(
	ToxI& t,
	ToxEventProviderI& tep,
	NGCEXTEventProviderI& neep
) : _t(t), _tep(tep), _neep(neep)
{
	_neep.subscribe(this, NGCEXT_Event::FT1_REQUEST);
	_neep.subscribe(this, NGCEXT_Event::FT1_INIT);
	_neep.subscribe(this, NGCEXT_Event::FT1_INIT_ACK);
	_neep.subscribe(this, NGCEXT_Event::FT1_DATA);
	_neep.subscribe(this, NGCEXT_Event::FT1_DATA_ACK);
	_neep.subscribe(this, NGCEXT_Event::FT1_MESSAGE);

	_tep.subscribe(this, Tox_Event::TOX_EVENT_GROUP_PEER_EXIT);
}

void NGCFT1::iterate(float time_delta) {
	for (auto& [group_number, group] : groups) {
		for (auto& [peer_number, peer] : group.peers) {
			iteratePeer(time_delta, group_number, peer_number, peer);
		}
	}
}

void NGCFT1::NGC_FT1_send_request_private(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size
) {
	// TODO: error check
	sendPKG_FT1_REQUEST(group_number, peer_number, file_kind, file_id, file_id_size);
}

bool NGCFT1::NGC_FT1_send_init_private(
	uint32_t group_number, uint32_t peer_number,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size,
	size_t file_size,
	uint8_t* transfer_id
) {
	if (std::get<0>(_t.toxGroupPeerGetConnectionStatus(group_number, peer_number)).value_or(TOX_CONNECTION_NONE) == TOX_CONNECTION_NONE) {
		std::cerr << "NGCFT1 error: cant init ft, peer offline\n";
		return false;
	}

	auto& peer = groups[group_number].peers[peer_number];

	// allocate transfer_id
	size_t idx = peer.next_send_transfer_idx;
	peer.next_send_transfer_idx = (peer.next_send_transfer_idx + 1) % 256;
	{ // TODO: extract
		size_t i = idx;
		bool found = false;
		do {
			if (!peer.send_transfers[i].has_value()) {
				// free slot
				idx = i;
				found = true;
				break;
			}

			i = (i + 1) % 256;
		} while (i != idx);

		if (!found) {
			std::cerr << "NGCFT1 error: cant init ft, no free transfer slot\n";
			return false;
		}
	}

	// TODO: check return value
	sendPKG_FT1_INIT(group_number, peer_number, file_kind, file_size, idx, file_id, file_id_size);

	peer.send_transfers[idx] = Group::Peer::SendTransfer{
		file_kind,
		std::vector(file_id, file_id+file_id_size),
		Group::Peer::SendTransfer::State::INIT_SENT,
		1,
		0.f,
		file_size,
		0,
		{}, // ssb
	};

	if (transfer_id != nullptr) {
		*transfer_id = idx;
	}

	return true;
}

bool NGCFT1::NGC_FT1_send_message_public(
	uint32_t group_number,
	uint32_t& message_id,
	uint32_t file_kind,
	const uint8_t* file_id, size_t file_id_size
) {
	// create msg_id
	message_id = randombytes_random();

	// TODO: check return value
	return sendPKG_FT1_MESSAGE(group_number, message_id, file_kind, file_id, file_id_size);
}

bool NGCFT1::onEvent(const Events::NGCEXT_ft1_request& e) {
//#if !NDEBUG
	std::cout << "NGCFT1: FT1_REQUEST fk:" << e.file_kind << " [" << bin2hex(e.file_id) << "]\n";
//#endif

	// .... just rethrow??
	// TODO: dont
	return dispatch(
		NGCFT1_Event::recv_request,
		Events::NGCFT1_recv_request{
			e.group_number, e.peer_number,
			static_cast<NGCFT1_file_kind>(e.file_kind),
			e.file_id.data(), e.file_id.size()
		}
	);
}

bool NGCFT1::onEvent(const Events::NGCEXT_ft1_init& e) {
//#if !NDEBUG
	std::cout << "NGCFT1: FT1_INIT fk:" << e.file_kind << " fs:" << e.file_size << " tid:" << int(e.transfer_id) << " [" << bin2hex(e.file_id) << "]\n";
//#endif

	bool accept = false;
	dispatch(
		NGCFT1_Event::recv_init,
		Events::NGCFT1_recv_init{
			e.group_number, e.peer_number,
			static_cast<NGCFT1_file_kind>(e.file_kind),
			e.file_id.data(), e.file_id.size(),
			e.transfer_id,
			e.file_size,
			accept
		}
	);

	if (!accept) {
		std::cout << "NGCFT1: rejected init\n";
		return true; // return true?
	}

	sendPKG_FT1_INIT_ACK(e.group_number, e.peer_number, e.transfer_id);

	std::cout << "NGCFT1: accepted init\n";

	auto& peer = groups[e.group_number].peers[e.peer_number];
	if (peer.recv_transfers[e.transfer_id].has_value()) {
		std::cerr << "NGCFT1 warning: overwriting existing recv_transfer " << int(e.transfer_id) << "\n";
	}

	peer.recv_transfers[e.transfer_id] = Group::Peer::RecvTransfer{
		e.file_kind,
		e.file_id,
		Group::Peer::RecvTransfer::State::INITED,
		e.file_size,
		0u,
		{} // rsb
	};

	return true;
}

bool NGCFT1::onEvent(const Events::NGCEXT_ft1_init_ack& e) {
//#if !NDEBUG
	std::cout << "NGCFT1: FT1_INIT_ACK\n";
//#endif

	// we now should start sending data

	if (!groups.count(e.group_number)) {
		std::cerr << "NGCFT1 warning: init_ack for unknown group\n";
		return true;
	}

	Group::Peer& peer = groups[e.group_number].peers[e.peer_number];
	if (!peer.send_transfers[e.transfer_id].has_value()) {
		std::cerr << "NGCFT1 warning: init_ack for unknown transfer\n";
		return true;
	}

	Group::Peer::SendTransfer& transfer = peer.send_transfers[e.transfer_id].value();

	using State = Group::Peer::SendTransfer::State;
	if (transfer.state != State::INIT_SENT) {
		std::cerr << "NGCFT1 error: inti_ack but not in INIT_SENT state\n";
		return true;
	}

	// iterate will now call NGC_FT1_send_data_cb
	transfer.state = State::SENDING;
	transfer.time_since_activity = 0.f;

	return true;
}

bool NGCFT1::onEvent(const Events::NGCEXT_ft1_data& e) {
#if !NDEBUG
	std::cout << "NGCFT1: FT1_DATA\n";
#endif

	if (e.data.empty()) {
		std::cerr << "NGCFT1 error: data of size 0!\n";
		return true;
	}

	if (!groups.count(e.group_number)) {
		std::cerr << "NGCFT1 warning: data for unknown group\n";
		return true;
	}

	Group::Peer& peer = groups[e.group_number].peers[e.peer_number];
	if (!peer.recv_transfers[e.transfer_id].has_value()) {
		std::cerr << "NGCFT1 warning: data for unknown transfer\n";
		return true;
	}

	auto& transfer = peer.recv_transfers[e.transfer_id].value();

	// do reassembly, ignore dups
	transfer.rsb.add(e.sequence_id, std::vector<uint8_t>(e.data)); // TODO: ugly explicit copy for what should just be a move

	// loop for chunks without holes
	while (transfer.rsb.canPop()) {
		auto data = transfer.rsb.pop();

		// TODO: check return value
		dispatch(
			NGCFT1_Event::recv_data,
			Events::NGCFT1_recv_data{
				e.group_number, e.peer_number,
				e.transfer_id,
				transfer.file_size_current,
				data.data(), data.size()
			}
		);

		transfer.file_size_current += data.size();
	}

	// send acks
	// reverse, last seq is most recent
	std::vector<uint16_t> ack_seq_ids(transfer.rsb.ack_seq_ids.crbegin(), transfer.rsb.ack_seq_ids.crend());
	// TODO: check if this caps at max acks
	if (!ack_seq_ids.empty()) {
		// TODO: check return value
		sendPKG_FT1_DATA_ACK(e.group_number, e.peer_number, e.transfer_id, ack_seq_ids.data(), ack_seq_ids.size());
	}


	if (transfer.file_size_current == transfer.file_size) {
		// TODO: set all data received, and clean up
		//transfer.state = Group::Peer::RecvTransfer::State::RECV;
		dispatch(
			NGCFT1_Event::recv_done,
			Events::NGCFT1_recv_done{
				e.group_number, e.peer_number,
				e.transfer_id
			}
		);
	}

	return true;
}

bool NGCFT1::onEvent(const Events::NGCEXT_ft1_data_ack& e) {
#if !NDEBUG
	//std::cout << "NGCFT1: FT1_DATA_ACK\n";
#endif

	if (!groups.count(e.group_number)) {
		std::cerr << "NGCFT1 warning: data_ack for unknown group\n";
		return true;
	}

	Group::Peer& peer = groups[e.group_number].peers[e.peer_number];
	if (!peer.send_transfers[e.transfer_id].has_value()) {
		std::cerr << "NGCFT1 warning: data_ack for unknown transfer\n";
		return true;
	}

	Group::Peer::SendTransfer& transfer = peer.send_transfers[e.transfer_id].value();

	using State = Group::Peer::SendTransfer::State;
	if (transfer.state != State::SENDING && transfer.state != State::FINISHING) {
		std::cerr << "NGCFT1 error: data_ack but not in SENDING or FINISHING state (" << int(transfer.state) << ")\n";
		return true;
	}

	transfer.time_since_activity = 0.f;

	{
		std::vector<CCAI::SeqIDType> seqs;
		for (const auto it : e.sequence_ids) {
			// TODO: improve this o.o
			seqs.push_back({e.transfer_id, it});
			transfer.ssb.erase(it);
		}
		peer.cca->onAck(std::move(seqs));
	}

	// delete if all packets acked
	if (transfer.file_size == transfer.file_size_current && transfer.ssb.size() == 0) {
		std::cout << "NGCFT1: " << int(e.transfer_id) << " done\n";
		dispatch(
			NGCFT1_Event::send_done,
			Events::NGCFT1_send_done{
				e.group_number, e.peer_number,
				e.transfer_id,
			}
		);
		// TODO: check for FINISHING state
		peer.send_transfers[e.transfer_id].reset();
	}

	return true;
}

bool NGCFT1::onEvent(const Events::NGCEXT_ft1_message& e) {
	std::cout << "NGCFT1: FT1_MESSAGE mid:" << e.message_id << " fk:" << e.file_kind << " [" << bin2hex(e.file_id) << "]\n";

	// .... just rethrow??
	// TODO: dont
	return dispatch(
		NGCFT1_Event::recv_message,
		Events::NGCFT1_recv_message{
			e.group_number, e.peer_number,
			e.message_id,
			static_cast<NGCFT1_file_kind>(e.file_kind),
			e.file_id.data(), e.file_id.size()
		}
	);
}

bool NGCFT1::onToxEvent(const Tox_Event_Group_Peer_Exit* e) {
	const auto group_number = tox_event_group_peer_exit_get_group_number(e);
	const auto peer_number = tox_event_group_peer_exit_get_peer_id(e);

	// peer disconnected, end all transfers

	if (!groups.count(group_number)) {
		return false;
	}

	auto& group = groups.at(group_number);

	if (!group.peers.count(peer_number)) {
		return false;
	}

	auto& peer = group.peers.at(peer_number);

	for (size_t i = 0; i < peer.send_transfers.size(); i++) {
		auto& it_opt = peer.send_transfers.at(i);
		if (!it_opt.has_value()) {
			continue;
		}

		std::cout << "NGCFT1: sending " << int(i) << " canceled bc peer offline\n";
		dispatch(
			NGCFT1_Event::send_done,
			Events::NGCFT1_send_done{
				group_number, peer_number,
				static_cast<uint8_t>(i),
			}
		);

		it_opt.reset();
	}

	for (size_t i = 0; i < peer.recv_transfers.size(); i++) {
		auto& it_opt = peer.recv_transfers.at(i);
		if (!it_opt.has_value()) {
			continue;
		}

		std::cout << "NGCFT1: receiving " << int(i) << " canceled bc peer offline\n";
		dispatch(
			NGCFT1_Event::recv_done,
			Events::NGCFT1_recv_done{
				group_number, peer_number,
				static_cast<uint8_t>(i),
			}
		);

		it_opt.reset();
	}

	// reset cca
	peer.cca = std::make_unique<CUBIC>(500-4); // TODO: replace with tox_group_max_custom_lossy_packet_length()-4

	return false;
}

