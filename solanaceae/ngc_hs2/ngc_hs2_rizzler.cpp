#include "./ngc_hs2_rizzler.hpp"

#include <solanaceae/contact/contact_store_i.hpp>
#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>
#include <solanaceae/message3/contact_components.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/tox_messages/msg_components.hpp>
#include <solanaceae/ngc_ft1/ngcft1_file_kind.hpp>

// TODO: move somewhere else?
#include <solanaceae/ngc_ft1_sha1/util.hpp>

#include <solanaceae/util/span.hpp>
#include <solanaceae/util/time.hpp>

#include <entt/entity/entity.hpp>

#include <nlohmann/json.hpp>

#include "./serl.hpp"

#include <cstdint>
#include <deque>
#include <cstring>

#include <iostream>

// TODO: move to own file
namespace Components {
	struct RequestedChatLogs {
		struct Entry {
			uint64_t ts_start;
			uint64_t ts_end;
			//std::vector<uint8_t> fid; // ?
		};
		std::deque<Entry> list;
		bool contains(uint64_t ts_start, uint64_t ts_end);
		void addRequest(uint64_t ts_start, uint64_t ts_end);
	};

	struct RunningChatLogs {
		struct Entry {
			uint64_t ts_start;
			uint64_t ts_end;
			std::vector<uint8_t> data;
			float last_activity {0.f};
		};
		// list of transfers
		entt::dense_map<uint8_t, Entry> list;
	};

	bool RequestedChatLogs::contains(uint64_t ts_start, uint64_t ts_end) {
		auto it = std::find_if(list.cbegin(), list.cend(), [ts_start, ts_end](const auto& value) {
			return value.ts_start == ts_start && value.ts_end == ts_end;
		});
		return it != list.cend();
	}

	void RequestedChatLogs::addRequest(uint64_t ts_start, uint64_t ts_end) {
		if (contains(ts_start, ts_end)) {
			return; // pre existing
		}
		list.push_back(Entry{ts_start, ts_end});
	}

} // Components

NGCHS2Rizzler::NGCHS2Rizzler(
	ContactStore4I& cs,
	RegistryMessageModelI& rmm,
	ToxContactModel2& tcm,
	NGCFT1& nft,
	ToxEventProviderI& tep,
	SHA1_NGCFT1& sha1_nft
) :
	_cs(cs),
	_rmm(rmm),
	_tcm(tcm),
	_nft(nft),
	_nftep_sr(_nft.newSubRef(this)),
	_tep_sr(tep.newSubRef(this)),
	_sha1_nft(sha1_nft)
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

		const ContactHandle4 c = _cs.contactHandle(it->first);

		if (!c.all_of<Contact::Components::ToxGroupPeerEphemeral>()) {
			// peer nolonger online
			it = _request_queue.erase(it);
			continue;
		}

		const auto [group_number, peer_number] = c.get<Contact::Components::ToxGroupPeerEphemeral>();

		// now in sec
		const uint64_t ts_now = getTimeMS()/1000;

		const uint64_t ts_start = ts_now;
		const uint64_t ts_end = ts_now-(60*60*48);

		if (sendRequest(group_number, peer_number, ts_start, ts_end)) {
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

			auto& rcl = c.get_or_emplace<Components::RequestedChatLogs>();
			rcl.addRequest(ts_start, ts_end);
		} else {
			// on failure, assume disconnected
		}

		// remove from request queue
		it = _request_queue.erase(it);
	}

	return 1000.f;
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

void NGCHS2Rizzler::handleMsgPack(ContactHandle4 sync_by_c, const std::vector<uint8_t>& data) {
	assert(sync_by_c);

	auto* reg_ptr = _rmm.get(sync_by_c);
	if (reg_ptr == nullptr) {
		std::cerr << "NGCHS2Rizzler error: group without msg reg\n";
		return;
	}

	Message3Registry& reg = *reg_ptr;

	uint64_t now_ts = getTimeMS();

	std::cout << "NGCHS2Rizzler: start parsing msgpack chatlog from " << entt::to_integral(sync_by_c.entity()) << "\n";
	try {
		const auto j = nlohmann::json::from_msgpack(data);
		if (!j.is_array()) {
			std::cerr << "NGCHS2Rizzler error: chatlog not array\n";
			return;
		}

		std::cout << "NGCHS2Rizzler: chatlog has " << j.size() << " entries\n";

		for (const auto j_entry : j) {
			try {
				// deci seconds
				uint64_t ts = j_entry.at("ts");
				// TODO: check against ts range

				ts *= 100; // convert to ms

				const uint64_t max_future_ms = 1u*60u*1000u; // accept up to 1 minute into the future
				if (ts - max_future_ms > now_ts) {
					// message is too far into the future
					continue;
				}

				const auto& j_ppk = j_entry.at("ppk");

				uint32_t mid = j_entry.at("mid");

				if (
					!(j_entry.count("text")) &&
					!(j_entry.count("fkind") && j_entry.count("fid"))
				) {
					std::cerr << "NGCHS2Rizzler error: msg neither contains text nor file fields\n";
					continue;
				}

				Contact4 from_c{entt::null};
				{ // from_c
					std::vector<uint8_t> id;
					if (j_ppk.is_binary()) {
						id = j_ppk.get_binary();
					} else {
						j_ppk.at("bytes").get_to(id);
					}

					const auto parent = sync_by_c.get<Contact::Components::Parent>().parent;

					from_c = _cs.getOneContactByID(parent, ByteSpan{id});

					auto& cr = _cs.registry();

					if (!cr.valid(from_c)) {
						// create sparse contact with id only
						from_c = cr.create();
						cr.emplace_or_replace<Contact::Components::ID>(from_c, id);

						// TODO: only if public message
						cr.emplace_or_replace<Contact::Components::Parent>(from_c, parent);

						_cs.throwEventConstruct(from_c);
					} else if (!cr.all_of<Contact::Components::Parent>(from_c)) {
						std::cerr << "NGCHS2Rizzler warning: from contact missing parent, assuming and force constructing! find and fix the cause!\n";
						// TODO: only if public message
						// we require this for file messages
						cr.emplace_or_replace<Contact::Components::Parent>(from_c, parent);
						_cs.throwEventUpdate(from_c);
					}
				}

				// TODO: from_c perm check
				// hard to do without numbers

				Message3Handle new_real_msg{reg, reg.create()};

				new_real_msg.emplace<Message::Components::Timestamp>(ts); // reactive?

				new_real_msg.emplace<Message::Components::ContactFrom>(from_c);
				new_real_msg.emplace<Message::Components::ContactTo>(sync_by_c.get<Contact::Components::Parent>().parent);

				new_real_msg.emplace<Message::Components::ToxGroupMessageID>(mid);

				if (j_entry.contains("action") && static_cast<bool>(j_entry.at("action"))) {
					new_real_msg.emplace<Message::Components::TagMessageIsAction>();
				}

				if (j_entry.contains("text")) {
					const std::string& text = j_entry.at("text");

					new_real_msg.emplace<Message::Components::MessageText>(text);

#if 0
					std::cout
						<< "msg ts:" << ts
						//<< " ppk:" << j_ppk
						<< " mid:" << mid
						<< " type:" << type
						<< " text:" << text
						<< "\n"
					;
#endif
				} else if (j_entry.contains("fkind") && j_entry.contains("fid")) {
					uint32_t fkind = j_entry.at("fkind");

					const auto& j_fid = j_entry.at("fid");

					std::vector<uint8_t> fid;
					if (j_fid.is_binary()) {
						fid = j_fid.get_binary();
					} else {
						j_fid.at("bytes").get_to(fid);
					}

					if (fkind == (uint32_t)NGCFT1_file_kind::HASH_SHA1_INFO) {
						_sha1_nft.constructFileMessageInPlace(
							new_real_msg,
							NGCFT1_file_kind::HASH_SHA1_INFO,
							ByteSpan{fid}
						);
					} else {
						std::cerr << "NGCHS2Rizzler error: unknown file kind " << fkind << "\n";
					}

#if 0
					std::cout
						<< "msg ts:" << ts
						//<< " ppk:" << j_ppk
						<< " mid:" << mid
						<< " type:" << type
						<< " fkind:" << fkind
						<< " fid:" << j_fid
						<< "\n"
					;
#endif
				}

				// now check against pre existing
				// TODO: dont do this afterwards
				Message3Handle dup_msg{};
				{ // check preexisting
					// get comparator from contact
					const ContactHandle4 reg_c = _cs.contactHandle(reg.ctx().get<Contact4>());
					if (reg_c.all_of<Contact::Components::MessageIsSame>()) {
						auto& comp = reg_c.get<Contact::Components::MessageIsSame>().comp;
						// walking EVERY existing message OOF
						// this needs optimizing
						for (const Message3 other_msg : reg.view<Message::Components::Timestamp, Message::Components::ContactFrom, Message::Components::ContactTo>()) {
							if (other_msg == new_real_msg) {
								continue; // skip self
							}

							if (comp({reg, other_msg}, new_real_msg)) {
								// dup
								dup_msg = {reg, other_msg};
								break;
							}
						}
					} // else, default heuristic??
				}

				Message3Handle new_msg = new_real_msg;

				if (dup_msg) {
					// we leak objects here (if file)
					reg.destroy(new_msg);
					new_msg = dup_msg;
				}

				{ // by whom
					auto& synced_by = new_msg.get_or_emplace<Message::Components::SyncedBy>().ts;
					// dont overwrite
					synced_by.try_emplace(sync_by_c, now_ts);
				}

				{ // now we also know they got the message
					auto& list = new_msg.get_or_emplace<Message::Components::ReceivedBy>().ts;
					// dont overwrite
					list.try_emplace(sync_by_c, now_ts);
				}

				if (new_msg == dup_msg) {
					// TODO: maybe update a timestamp?
					_rmm.throwEventUpdate(reg, new_msg);
				} else {
					// pure new msg

					new_msg.emplace<Message::Components::TimestampProcessed>(now_ts);
					new_msg.emplace<Message::Components::TimestampWritten>(ts);

					new_msg.emplace<Message::Components::TagUnread>();
					_rmm.throwEventConstruct(reg, new_msg);
				}
			} catch (...) {
				std::cerr << "NGCHS2Rizzler error: parsing entry '" << j_entry.dump() << "'\n";
			}
		}
	} catch (...) {
		std::cerr << "NGCHS2Rizzler error: failed parsing data as msgpack\n";
	}
}

bool NGCHS2Rizzler::onEvent(const Events::NGCFT1_recv_init& e) {
	if (e.file_kind != NGCFT1_file_kind::HS2_RANGE_TIME_MSGPACK) {
		return false; // not for us
	}

	std::cout << "NGCHS2Rizzler: recv_init " << e.group_number << ":" << e.peer_number << "." << (int)e.transfer_id << "\n";

	auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	if (!c) {
		return false; // huh?
	}

	if (!c.all_of<Components::RequestedChatLogs>()) {
		return false;
	}

	// parse start end
	// TODO: extract
	ByteSpan fid{e.file_id, e.file_id_size};
	// TODO: better size check
	if (fid.size != sizeof(uint64_t)+sizeof(uint64_t)) {
		std::cerr << "NGCHS2S error: range not lange enough\n";
		return true;
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
		std::cerr << "NGCHS2R error: failed to parse range\n";
		return true;
	}

	if (ts_end >= ts_start) {
		std::cerr << "NGCHS2R error: end not < start\n";
		return true;
	}

	auto& reqcl = c.get<Components::RequestedChatLogs>();

	if (!reqcl.contains(ts_start, ts_end)) {
		// warn?
		return true;
	}

	auto& rnncl = c.get_or_emplace<Components::RunningChatLogs>();
	_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // cache

	auto& transfer = rnncl.list[e.transfer_id];
	transfer.data.reserve(e.file_size); // danger?
	transfer.last_activity = 0.f;
	transfer.ts_start = ts_start;
	transfer.ts_end = ts_end;

	e.accept = true;

	return true;
}

bool NGCHS2Rizzler::onEvent(const Events::NGCFT1_recv_data& e) {
	auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	if (!c) {
		return false;
	}

	if (!c.all_of<Components::RunningChatLogs>()) {
		return false; // not ours
	}

	auto& rnncl = c.get<Components::RunningChatLogs>();
	if (!rnncl.list.count(e.transfer_id)) {
		return false; // not ours
	}

	std::cout << "NGCHS2Rizzler: recv_data " << e.group_number << ":" << e.peer_number << "." << (int)e.transfer_id << " " << e.data_size << "@" << e.data_offset << "\n";

	auto& transfer = rnncl.list.at(e.transfer_id);
	transfer.data.resize(e.data_offset+e.data_size);
	std::memcpy(&transfer.data[e.data_offset], e.data, e.data_size);

	transfer.last_activity = 0.f;

	return true;
}

bool NGCHS2Rizzler::onEvent(const Events::NGCFT1_recv_done& e) {
	// FIXME: this does not work, tcm just delteded the relation ship
	//auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	//if (!c) {
	//    return false;
	//}
	const auto c_it = _tox_peer_to_contact.find(combine_ids(e.group_number, e.peer_number));
	if (c_it == _tox_peer_to_contact.end()) {
		return false;
	}
	auto c = c_it->second;
	if (!static_cast<bool>(c)) {
		return false;
	}

	if (!c.all_of<Components::RunningChatLogs>()) {
		return false; // not ours
	}

	auto& rnncl = c.get<Components::RunningChatLogs>();
	if (!rnncl.list.count(e.transfer_id)) {
		return false; // not ours
	}

	std::cout << "NGCHS2Rizzler: recv_done " << e.group_number << ":" << e.peer_number << "." << (int)e.transfer_id << "\n";
	{
		auto& transfer = rnncl.list.at(e.transfer_id);
		// TODO: done might mean failed, so we might be parsing bs here

		// use data
		// TODO: move out of packet handler
		handleMsgPack(c, transfer.data);
	}
	rnncl.list.erase(e.transfer_id);

	return true;
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

