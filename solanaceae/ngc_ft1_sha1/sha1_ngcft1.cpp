#include "./sha1_ngcft1.hpp"

#include <solanaceae/util/utils.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/tox_messages/components.hpp>

#include "./util.hpp"

#include "./ft1_sha1_info.hpp"
#include "./hash_utils.hpp"

#include <sodium.h>

#include <entt/container/dense_set.hpp>

#include "./file_rw_mapped.hpp"

#include "./components.hpp"
#include "./chunk_picker.hpp"
#include "./participation.hpp"

#include <iostream>
#include <variant>
#include <filesystem>
#include <mutex>
#include <future>
#include <vector>

namespace Message::Components {

	using Content = ObjectHandle;

} // Message::Components

static size_t chunkSize(const FT1InfoSHA1& sha1_info, size_t chunk_index) {
	if (chunk_index+1 == sha1_info.chunks.size()) {
		// last chunk
		return sha1_info.file_size - chunk_index * sha1_info.chunk_size;
	} else {
		return sha1_info.chunk_size;
	}
}

void SHA1_NGCFT1::queueUpRequestChunk(uint32_t group_number, uint32_t peer_number, ObjectHandle content, const SHA1Digest& hash) {
	for (auto& [i_g, i_p, i_o, i_h, i_t] : _queue_requested_chunk) {
		// if already in queue
		if (i_g == group_number && i_p == peer_number && i_h == hash) {
			// update timer
			i_t = 0.f;
			return;
		}
	}

	// check for running transfer
	if (_sending_transfers.count(combine_ids(group_number, peer_number))) {
		for (const auto& [_, transfer] : _sending_transfers.at(combine_ids(group_number, peer_number))) {
			if (std::holds_alternative<SendingTransfer::Info>(transfer.v)) {
				// ignore info
				continue;
			}

			const auto& t_c = std::get<SendingTransfer::Chunk>(transfer.v);

			if (content != t_c.content) {
				// ignore different content
				continue;
			}

			auto chunk_idx_vec = content.get<Components::FT1ChunkSHA1Cache>().chunkIndices(hash);

			for (size_t idx : chunk_idx_vec) {
				if (idx == t_c.chunk_index) {
					// already sending
					return; // skip
				}
			}
		}
	}

	// not in queue yet
	_queue_requested_chunk.push_back(std::make_tuple(group_number, peer_number, content, hash, 0.f));
}

void SHA1_NGCFT1::updateMessages(ObjectHandle ce) {
	assert(ce.all_of<Components::Messages>());

	for (auto msg : ce.get<Components::Messages>().messages) {
		if (ce.all_of<Message::Components::Transfer::FileInfo>() && !msg.all_of<Message::Components::Transfer::FileInfo>()) {
			msg.emplace<Message::Components::Transfer::FileInfo>(ce.get<Message::Components::Transfer::FileInfo>());
		}
		if (ce.all_of<Message::Components::Transfer::FileInfoLocal>()) {
			msg.emplace_or_replace<Message::Components::Transfer::FileInfoLocal>(ce.get<Message::Components::Transfer::FileInfoLocal>());
		}
		if (ce.all_of<Message::Components::Transfer::BytesSent>()) {
			msg.emplace_or_replace<Message::Components::Transfer::BytesSent>(ce.get<Message::Components::Transfer::BytesSent>());
		}
		if (ce.all_of<Message::Components::Transfer::BytesReceived>()) {
			msg.emplace_or_replace<Message::Components::Transfer::BytesReceived>(ce.get<Message::Components::Transfer::BytesReceived>());
		}
		if (ce.all_of<Message::Components::Transfer::TagPaused>()) {
			msg.emplace_or_replace<Message::Components::Transfer::TagPaused>();
		} else {
			msg.remove<Message::Components::Transfer::TagPaused>();
		}
		if (auto* cc = ce.try_get<Components::FT1ChunkSHA1Cache>(); cc != nullptr && cc->have_all) {
			msg.emplace_or_replace<Message::Components::Transfer::TagHaveAll>();
		}

		_rmm.throwEventUpdate(msg);
	}
}

std::optional<std::pair<uint32_t, uint32_t>> SHA1_NGCFT1::selectPeerForRequest(ObjectHandle ce) {
	// get a list of peers we can request this file from
	std::vector<std::pair<uint32_t, uint32_t>> tox_peers;
	for (const auto c : ce.get<Components::SuspectedParticipants>().participants) {
		// TODO: sort by con state?
		// prio to direct?
		if (const auto* cs = _cr.try_get<Contact::Components::ConnectionState>(c); cs == nullptr || cs->state == Contact::Components::ConnectionState::State::disconnected) {
			continue;
		}

		if (_cr.all_of<Contact::Components::ToxGroupPeerEphemeral>(c)) {
			const auto& tgpe = _cr.get<Contact::Components::ToxGroupPeerEphemeral>(c);
			tox_peers.push_back({tgpe.group_number, tgpe.peer_number});
		}
	}

	// 1 in 40 chance to ask random peer instead
	// TODO: config + tweak
	// TODO: save group in content to avoid the tox_peers list build
	// TODO: remove once pc1_announce is shipped
	if (tox_peers.empty() || (_rng()%40) == 0) {
		// meh
		// HACK: determain group based on last tox_peers
		if (!tox_peers.empty()) {
			const uint32_t group_number = tox_peers.back().first;
			auto gch = _tcm.getContactGroup(group_number);
			assert(static_cast<bool>(gch));

			std::vector<uint32_t> un_tox_peers;
			for (const auto child : gch.get<Contact::Components::ParentOf>().subs) {
				if (const auto* cs = _cr.try_get<Contact::Components::ConnectionState>(child); cs == nullptr || cs->state == Contact::Components::ConnectionState::State::disconnected) {
					continue;
				}

				if (_cr.all_of<Contact::Components::TagSelfStrong>(child)) {
					continue; // skip self
				}

				if (_cr.all_of<Contact::Components::ToxGroupPeerEphemeral>(child)) {
					const auto& tgpe = _cr.get<Contact::Components::ToxGroupPeerEphemeral>(child);
					un_tox_peers.push_back(tgpe.peer_number);
				}
			}
			if (un_tox_peers.empty()) {
				// no one online, we are out of luck
			} else {
				const size_t sample_i = _rng()%un_tox_peers.size();
				const auto peer_number = un_tox_peers.at(sample_i);

				return std::make_pair(group_number, peer_number);
			}
		}
	} else {
		const size_t sample_i = _rng()%tox_peers.size();
		const auto [group_number, peer_number] = tox_peers.at(sample_i);

		return std::make_pair(group_number, peer_number);
	}

	return std::nullopt;
}

SHA1_NGCFT1::SHA1_NGCFT1(
	ObjectStore2& os,
	Contact3Registry& cr,
	RegistryMessageModel& rmm,
	NGCFT1& nft,
	ToxContactModel2& tcm,
	ToxEventProviderI& tep,
	NGCEXTEventProvider& neep
) :
	_os(os),
	_cr(cr),
	_rmm(rmm),
	_nft(nft),
	_tcm(tcm),
	_tep(tep),
	_neep(neep)
{
	// TODO: also create and destroy
	_rmm.subscribe(this, RegistryMessageModel_Event::message_updated);

	_nft.subscribe(this, NGCFT1_Event::recv_request);
	_nft.subscribe(this, NGCFT1_Event::recv_init);
	_nft.subscribe(this, NGCFT1_Event::recv_data);
	_nft.subscribe(this, NGCFT1_Event::send_data);
	_nft.subscribe(this, NGCFT1_Event::recv_done);
	_nft.subscribe(this, NGCFT1_Event::send_done);
	_nft.subscribe(this, NGCFT1_Event::recv_message);

	//_rmm.subscribe(this, RegistryMessageModel_Event::message_construct);
	//_rmm.subscribe(this, RegistryMessageModel_Event::message_updated);
	//_rmm.subscribe(this, RegistryMessageModel_Event::message_destroy);

	_rmm.subscribe(this, RegistryMessageModel_Event::send_file_path);

	_tep.subscribe(this, Tox_Event_Type::TOX_EVENT_GROUP_PEER_EXIT);

	_neep.subscribe(this, NGCEXT_Event::PC1_ANNOUNCE);
	_neep.subscribe(this, NGCEXT_Event::FT1_HAVE);
	_neep.subscribe(this, NGCEXT_Event::FT1_BITSET);
}

void SHA1_NGCFT1::iterate(float delta) {
	// info builder queue
	if (_info_builder_dirty) {
		std::lock_guard l{_info_builder_queue_mutex};
		_info_builder_dirty = false; // set while holding lock

		for (auto& it : _info_builder_queue) {
			//it.fn();
			it();
		}
		_info_builder_queue.clear();
	}

	{ // timers
		// sending transfers
		for (auto peer_it = _sending_transfers.begin(); peer_it != _sending_transfers.end();) {
			for (auto it = peer_it->second.begin(); it != peer_it->second.end();) {
				it->second.time_since_activity += delta;

				// if we have not heard for 2min, timeout (lower level event on real timeout)
				// TODO: do we really need this if we get events?
				if (it->second.time_since_activity >= 120.f) {
					std::cerr << "SHA1_NGCFT1 warning: sending tansfer timed out " << "." << int(it->first) << "\n";
					it = peer_it->second.erase(it);
				} else {
					it++;
				}
			}

			if (peer_it->second.empty()) {
				// cleanup unused peers too agressive?
				peer_it = _sending_transfers.erase(peer_it);
			} else {
				peer_it++;
			}
		}

		// receiving transfers
		_receiving_transfers.tick(delta);

		// queued requests
		for (auto it = _queue_requested_chunk.begin(); it != _queue_requested_chunk.end();) {
			float& timer = std::get<float>(*it);
			timer += delta;

			// forget after 10sec
			if (timer >= 10.f) {
				it = _queue_requested_chunk.erase(it);
			} else {
				it++;
			}
		}

		{ // requested info timers
			std::vector<Object> timed_out;
			_os.registry().view<Components::ReRequestInfoTimer>().each([delta, &timed_out](Object e, Components::ReRequestInfoTimer& rrit) {
				rrit.timer += delta;

				// 15sec, TODO: config
				if (rrit.timer >= 15.f) {
					timed_out.push_back(e);
				}
			});
			for (const auto e : timed_out) {
				// TODO: avoid dups
				_queue_content_want_info.push_back(_os.objectHandle(e));
				_os.registry().remove<Components::ReRequestInfoTimer>(e);
				// TODO: throw update?
			}
		}
		{ // requested chunk timers
			_os.registry().view<Components::FT1ChunkSHA1Requested>().each([delta](Components::FT1ChunkSHA1Requested& ftchunk_requested) {
				for (auto it = ftchunk_requested.chunks.begin(); it != ftchunk_requested.chunks.end();) {
					it->second += delta;

					// 15sec, TODO: config
					if (it->second >= 15.f) {
						it = ftchunk_requested.chunks.erase(it);
					} else {
						it++;
					}
				}
			});
		}
	}

	// if we have not reached the total cap for transfers
	// count running transfers
	size_t running_sending_transfer_count {0};
	for (const auto& [_, transfers] : _sending_transfers) {
		running_sending_transfer_count += transfers.size();
	}
	size_t running_receiving_transfer_count {_receiving_transfers.size()};

	if (running_sending_transfer_count < _max_concurrent_out) {
		// TODO: for each peer? transfer cap per peer?
		// TODO: info queue
		if (!_queue_requested_chunk.empty()) { // then check for chunk requests
			const auto [group_number, peer_number, ce, chunk_hash, _] = _queue_requested_chunk.front();

			auto chunk_idx_vec = ce.get<Components::FT1ChunkSHA1Cache>().chunkIndices(chunk_hash);
			if (!chunk_idx_vec.empty()) {

				// check if already sending
				bool already_sending_to_this_peer = false;
				if (_sending_transfers.count(combine_ids(group_number, peer_number))) {
					for (const auto& [_2, t] : _sending_transfers.at(combine_ids(group_number, peer_number))) {
						if (std::holds_alternative<SendingTransfer::Chunk>(t.v)) {
							const auto& v = std::get<SendingTransfer::Chunk>(t.v);
							if (v.content == ce && v.chunk_index == chunk_idx_vec.front()) {
								// already sending
								already_sending_to_this_peer = true;
								break;
							}
						}
					}
				}

				if (!already_sending_to_this_peer) {
					const auto& info = ce.get<Components::FT1InfoSHA1>();

					uint8_t transfer_id {0};
					if (_nft.NGC_FT1_send_init_private(
						group_number, peer_number,
						static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_CHUNK),
						chunk_hash.data.data(), chunk_hash.size(),
						chunkSize(info, chunk_idx_vec.front()),
						&transfer_id
					)) {
						_sending_transfers
							[combine_ids(group_number, peer_number)]
							[transfer_id] // TODO: also save index?
								.v = SendingTransfer::Chunk{ce, chunk_idx_vec.front()};
					}
				} // else just remove from queue
			}
			// remove from queue regardless
			_queue_requested_chunk.pop_front();
		}
	}

	if (running_receiving_transfer_count < _max_concurrent_in) {
		// strictly priorize info
		if (!_queue_content_want_info.empty()) {
			const auto ce = _queue_content_want_info.front();

			// make sure we are missing the info
			assert(!ce.all_of<Components::ReRequestInfoTimer>());
			assert(!ce.all_of<Components::FT1InfoSHA1>());
			assert(!ce.all_of<Components::FT1InfoSHA1Data>());
			assert(!ce.all_of<Components::FT1ChunkSHA1Cache>());
			assert(ce.all_of<Components::FT1InfoSHA1Hash>());

			auto selected_peer_opt = selectPeerForRequest(ce);
			if (selected_peer_opt.has_value()) {
				const auto [group_number, peer_number] = selected_peer_opt.value();

				//const auto& info = msg.get<Components::FT1InfoSHA1>();
				const auto& info_hash = ce.get<Components::FT1InfoSHA1Hash>().hash;

				_nft.NGC_FT1_send_request_private(
					group_number, peer_number,
					static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO),
					info_hash.data(), info_hash.size()
				);
				ce.emplace<Components::ReRequestInfoTimer>(0.f);

				_queue_content_want_info.pop_front();

				std::cout << "SHA1_NGCFT1: sent info request for [" << SHA1Digest{info_hash} << "] to " << group_number << ":" << peer_number << "\n";
			}
#if 0
		} else if (!_queue_content_want_chunk.empty()) {
			const auto ce = _queue_content_want_chunk.front();

			auto& requested_chunks = ce.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks;
			if (requested_chunks.size() < _max_pending_requests) {

				// select chunk/make sure we still need one
				auto selected_peer_opt = selectPeerForRequest(ce);
				if (selected_peer_opt.has_value()) {
					const auto [group_number, peer_number] = selected_peer_opt.value();
					//std::cout << "SHA1_NGCFT1: should ask " << group_number << ":" << peer_number << " for content here\n";
					auto& cc = ce.get<Components::FT1ChunkSHA1Cache>();
					const auto& info = ce.get<Components::FT1InfoSHA1>();

					if (cc.have_all) {
						_queue_content_want_chunk.pop_front();
					} else {
						// naive, choose first chunk we dont have (double requests!!)
						// TODO: piece picker, choose what other have (invert selectPeerForRequest)
						for (size_t chunk_idx = 0; chunk_idx < info.chunks.size() /* cc.total_ */; chunk_idx++) {
							if (cc.have_chunk[chunk_idx]) {
								continue;
							}

							// check by hash
							if (cc.haveChunk(info.chunks.at(chunk_idx))) {
								// TODO: fix this, a completed chunk should fill all the indecies it occupies
								cc.have_chunk.set(chunk_idx);
								cc.have_count += 1;
								if (cc.have_count == info.chunks.size()) {
									cc.have_all = true;
									cc.have_chunk = BitSet(0); // conserve space
									break;
								}
								continue;
							}

							if (requested_chunks.count(chunk_idx)) {
								// already requested
								continue;
							}

							// request chunk_idx
							_nft.NGC_FT1_send_request_private(
								group_number, peer_number,
								static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_CHUNK),
								info.chunks.at(chunk_idx).data.data(), info.chunks.at(chunk_idx).size()
							);
							requested_chunks[chunk_idx] = 0.f;
							std::cout << "SHA1_NGCFT1: requesting chunk [" << info.chunks.at(chunk_idx) << "] from " << group_number << ":" << peer_number << "\n";

							break;
						}

						// ...

						// TODO: properly determine
						if (!cc.have_all) {
							_queue_content_want_chunk.push_back(ce);
						}
						_queue_content_want_chunk.pop_front();
					}
				}
			}
#endif
		}

		// new chunk picker code
		_cr.view<ChunkPicker>().each([this](const Contact3 cv, ChunkPicker& cp) {
			Contact3Handle c{_cr, cv};
			// HACK: expensive, dont do every tick, only on events
			// do verification in debug instead?
			cp.updateParticipation(
				c,
				_os.registry()
			);

			assert(!cp.participating.empty());

			auto new_requests = cp.updateChunkRequests(
				c,
				_os.registry(),
				_receiving_transfers
			);

			if (new_requests.empty()) {
				return;
			}

			assert(c.all_of<Contact::Components::ToxGroupPeerEphemeral>());
			const auto [group_number, peer_number] = c.get<Contact::Components::ToxGroupPeerEphemeral>();

			for (const auto [r_o, r_idx] : new_requests) {
				auto& cc = r_o.get<Components::FT1ChunkSHA1Cache>();
				const auto& info = r_o.get<Components::FT1InfoSHA1>();

				// request chunk_idx
				_nft.NGC_FT1_send_request_private(
					group_number, peer_number,
					static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_CHUNK),
					info.chunks.at(r_idx).data.data(), info.chunks.at(r_idx).size()
				);
				std::cout << "SHA1_NGCFT1: requesting chunk [" << info.chunks.at(r_idx) << "] from " << group_number << ":" << peer_number << "\n";
			}
		});
	}
}

bool SHA1_NGCFT1::onEvent(const Message::Events::MessageUpdated& e) {
	// see tox_transfer_manager.cpp for reference
	if (!e.e.all_of<Message::Components::Transfer::ActionAccept, Message::Components::Content>()) {
		return false;
	}

	//accept(e.e, e.e.get<Message::Components::Transfer::ActionAccept>().save_to_path);
	auto ce = e.e.get<Message::Components::Content>();

	//if (!ce.all_of<Components::FT1InfoSHA1, Components::FT1ChunkSHA1Cache>()) {
	if (!ce.all_of<Components::FT1InfoSHA1>()) {
		// not ready to load yet, skip
		return false;
	}
	assert(!ce.all_of<Components::FT1ChunkSHA1Cache>());
	assert(!ce.all_of<Message::Components::Transfer::File>());

	// first, open file for write(+readback)
	std::string full_file_path{e.e.get<Message::Components::Transfer::ActionAccept>().save_to_path};
	// TODO: replace with filesystem or something
	if (full_file_path.back() != '/') {
		full_file_path += "/";
	}

	// ensure dir exists
	std::filesystem::create_directories(full_file_path);

	const auto& info = ce.get<Components::FT1InfoSHA1>();
	full_file_path += info.file_name;

	ce.emplace<Message::Components::Transfer::FileInfoLocal>(std::vector{full_file_path});

	const bool file_exists = std::filesystem::exists(full_file_path);
	std::unique_ptr<File2I> file_impl = std::make_unique<File2RWMapped>(full_file_path, info.file_size);

	if (!file_impl->isGood()) {
		std::cerr << "SHA1_NGCFT1 error: failed opening file '" << full_file_path << "'!\n";
		// we failed opening that filepath, so we should offer the user the oportunity to save it differently
		e.e.remove<Message::Components::Transfer::ActionAccept>(); // stop
		return false;
	}

	{ // next, create chuck cache and check for existing data
		auto& cc = ce.emplace<Components::FT1ChunkSHA1Cache>();
		auto& bytes_received = ce.get_or_emplace<Message::Components::Transfer::BytesReceived>().total;
		cc.have_chunk = BitSet(info.chunks.size());
		cc.have_all = false;
		cc.have_count = 0;

		cc.chunk_hash_to_index.clear(); // if copy pasta

		if (file_exists) {
			// iterate existing file
			for (size_t i = 0; i < info.chunks.size(); i++) {
				const uint64_t chunk_size = info.chunkSize(i);
				auto existing_data = file_impl->read(chunk_size, i*uint64_t(info.chunk_size));

				assert(existing_data.size == chunk_size);
				if (existing_data.size == chunk_size) {
					const auto data_hash = SHA1Digest{hash_sha1(existing_data.ptr, existing_data.size)};
					const bool data_equal = data_hash == info.chunks.at(i);

					if (data_equal) {
						cc.have_chunk.set(i);
						cc.have_count += 1;
						bytes_received += chunk_size;
						//std::cout << "existing i[" << info.chunks.at(i) << "] == d[" << data_hash << "]\n";
					} else {
						//std::cout << "unk i[" << info.chunks.at(i) << "] != d[" << data_hash << "]\n";
					}
				} else {
					// error reading?
				}

				_chunks[info.chunks[i]] = ce;
				cc.chunk_hash_to_index[info.chunks[i]].push_back(i);
			}
			std::cout << "preexisting " << cc.have_count << "/" << info.chunks.size() << "\n";

			if (cc.have_count >= info.chunks.size()) {
				cc.have_all = true;
				//ce.remove<Message::Components::Transfer::BytesReceived>();
			}
		} else {
			for (size_t i = 0; i < info.chunks.size(); i++) {
				_chunks[info.chunks[i]] = ce;
				cc.chunk_hash_to_index[info.chunks[i]].push_back(i);
			}
		}

		if (!cc.have_all) {
			// now, enque
			_queue_content_want_chunk.push_back(ce);
		}
	}

	ce.emplace<Message::Components::Transfer::File>(std::move(file_impl));

	// announce we are participating
	// since this is the first time, we publicly announce to all
	if (e.e.all_of<Message::Components::ContactFrom, Message::Components::ContactTo>()) {
		const auto c_f = e.e.get<Message::Components::ContactFrom>().c;
		const auto c_t = e.e.get<Message::Components::ContactTo>().c;

		std::vector<uint8_t> announce_id;
		const uint32_t file_kind = static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO);
		for (size_t i = 0; i < sizeof(file_kind); i++) {
			announce_id.push_back((file_kind>>(i*8)) & 0xff);
		}
		assert(ce.all_of<Components::FT1InfoSHA1Hash>());
		const auto& info_hash = ce.get<Components::FT1InfoSHA1Hash>().hash;
		announce_id.insert(announce_id.cend(), info_hash.cbegin(), info_hash.cend());

		if (_cr.all_of<Contact::Components::ToxGroupEphemeral>(c_t)) {
			// public
			const auto group_number = _cr.get<Contact::Components::ToxGroupEphemeral>(c_t).group_number;

			_neep.send_all_pc1_announce(group_number, announce_id.data(), announce_id.size());
		} else if (_cr.all_of<Contact::Components::ToxGroupPeerEphemeral>(c_f)) {
			// private ?
			const auto [group_number, peer_number] = _cr.get<Contact::Components::ToxGroupPeerEphemeral>(c_f);

			_neep.send_pc1_announce(group_number, peer_number, announce_id.data(), announce_id.size());
		}
	}

	ce.remove<Message::Components::Transfer::TagPaused>();

	// should?
	e.e.remove<Message::Components::Transfer::ActionAccept>();

	updateMessages(ce);

	return false;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_request& e) {
	// only interested in sha1
	if (e.file_kind != NGCFT1_file_kind::HASH_SHA1_INFO && e.file_kind != NGCFT1_file_kind::HASH_SHA1_CHUNK) {
		return false;
	}

	//std::cout << "SHA1_NGCFT1: FT1_REQUEST fk:" << int(e.file_kind) << " [" << bin2hex({e.file_id, e.file_id+e.file_id_size}) << "]\n";

	if (e.file_kind == NGCFT1_file_kind::HASH_SHA1_INFO) {
		if (e.file_id_size != 20) {
			// error
			return false;
		}

		SHA1Digest info_hash{e.file_id, e.file_id_size};
		if (!_info_to_content.count(info_hash)) {
			// we dont know about this
			return false;
		}

		auto content = _info_to_content.at(info_hash);

		if (!content.all_of<Components::FT1InfoSHA1Data>()) {
			// we dont have the info for that infohash (yet?)
			return false;
		}

		// TODO: queue instead
		//queueUpRequestInfo(e.group_number, e.peer_number, info_hash);
		uint8_t transfer_id {0};
		_nft.NGC_FT1_send_init_private(
			e.group_number, e.peer_number,
			static_cast<uint32_t>(e.file_kind),
			e.file_id, e.file_id_size,
			content.get<Components::FT1InfoSHA1Data>().data.size(),
			&transfer_id
		);

		_sending_transfers
			[combine_ids(e.group_number, e.peer_number)]
			[transfer_id]
				.v = SendingTransfer::Info{content.get<Components::FT1InfoSHA1Data>().data};
	} else if (e.file_kind == NGCFT1_file_kind::HASH_SHA1_CHUNK) {
		if (e.file_id_size != 20) {
			// error
			return false;
		}

		SHA1Digest chunk_hash{e.file_id, e.file_id_size};

		if (!_chunks.count(chunk_hash)) {
			// we dont know about this
			return false;
		}

		auto o = _chunks.at(chunk_hash);

		{ // they advertise interest in the content
			const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
			addParticipation(c, o);
		}

		assert(o.all_of<Components::FT1ChunkSHA1Cache>());

		if (!o.get<Components::FT1ChunkSHA1Cache>().haveChunk(chunk_hash)) {
			// we dont have the chunk
			return false;
		}

		// queue good request
		queueUpRequestChunk(e.group_number, e.peer_number, o, chunk_hash);
	} else {
		assert(false && "unhandled case");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_init& e) {
	// only interested in sha1
	if (e.file_kind != NGCFT1_file_kind::HASH_SHA1_INFO && e.file_kind != NGCFT1_file_kind::HASH_SHA1_CHUNK) {
		return false;
	}

	// TODO: make sure we requested this?

	if (e.file_kind == NGCFT1_file_kind::HASH_SHA1_INFO) {
		SHA1Digest sha1_info_hash {e.file_id, e.file_id_size};
		if (!_info_to_content.count(sha1_info_hash)) {
			// no idea about this content
			return false;
		}

		auto ce = _info_to_content.at(sha1_info_hash);

		if (ce.any_of<Components::FT1InfoSHA1, Components::FT1InfoSHA1Data, Components::FT1ChunkSHA1Cache>()) {
			// we already have the info (should)
			return false;
		}

		// TODO: check if e.file_size too large / ask for permission
		if (e.file_size > 100*1024*1024) {
			// a info size of 100MiB is ~640GiB for a 128KiB chunk size (default)
			return false;
		}

		_receiving_transfers.emplaceInfo(
			e.group_number, e.peer_number,
			e.transfer_id,
			{ce, std::vector<uint8_t>(e.file_size)}
		);

		e.accept = true;
	} else if (e.file_kind == NGCFT1_file_kind::HASH_SHA1_CHUNK) {
		SHA1Digest sha1_chunk_hash {e.file_id, e.file_id_size};

		if (!_chunks.count(sha1_chunk_hash)) {
			// no idea about this content
			return false;
		}

		auto o = _chunks.at(sha1_chunk_hash);

		{ // they have the content (probably, might be fake, should move this to done)
			const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
			addParticipation(c, o);
		}

		assert(o.all_of<Components::FT1InfoSHA1>());
		assert(o.all_of<Components::FT1ChunkSHA1Cache>());

		const auto& cc = o.get<Components::FT1ChunkSHA1Cache>();
		if (cc.haveChunk(sha1_chunk_hash)) {
			std::cout << "SHA1_NGCFT1: chunk rejected, already have [" << SHA1Digest{sha1_chunk_hash} << "]\n";
			// we have the chunk
			return false;
		}
		// TODO: cache position

		// calc offset_into_file
		auto idx_vec = cc.chunkIndices(sha1_chunk_hash);
		assert(!idx_vec.empty());

		// CHECK IF TRANSFER IN PROGESS!!
		for (const auto idx : idx_vec) {
			if (_receiving_transfers.containsPeerChunk(e.group_number, e.peer_number, o, idx)) {
				std::cerr << "SHA1_NGCFT1 error: " << e.group_number << ":" << e.peer_number << " offered chunk(" << idx << ") it is already receiving!!\n";
				return false;
			}
		}

		const auto& info = o.get<Components::FT1InfoSHA1>();

		// TODO: check e.file_size
		assert(e.file_size == info.chunkSize(idx_vec.front()));

		_receiving_transfers.emplaceChunk(
			e.group_number, e.peer_number,
			e.transfer_id,
			ReceivingTransfers::Entry::Chunk{o, idx_vec}
		);

		e.accept = true;

		std::cout << "SHA1_NGCFT1: accepted chunk [" << SHA1Digest{sha1_chunk_hash} << "]\n";
	} else {
		assert(false && "unhandled case");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_data& e) {
	if (!_receiving_transfers.containsPeerTransfer(e.group_number, e.peer_number, e.transfer_id)) {
		return false;
	}

	auto& transfer = _receiving_transfers.getTransfer(e.group_number, e.peer_number, e.transfer_id);

	transfer.time_since_activity = 0.f;
	if (transfer.isInfo()) {
		auto& info_data = transfer.getInfo().info_data;
		for (size_t i = 0; i < e.data_size && i + e.data_offset < info_data.size(); i++) {
			info_data[i+e.data_offset] = e.data[i];
		}
	} else if (transfer.isChunk()) {
		auto o = transfer.getChunk().content;

		assert(o.all_of<Message::Components::Transfer::File>());
		auto* file = o.get<Message::Components::Transfer::File>().get();
		assert(file != nullptr);

		const auto chunk_size = o.get<Components::FT1InfoSHA1>().chunk_size;
		for (const auto chunk_index : transfer.getChunk().chunk_indices) {
			const auto offset_into_file = chunk_index * chunk_size;

			if (!file->write({e.data, e.data_size}, offset_into_file + e.data_offset)) {
				std::cerr << "SHA1_NGCFT1 error: writing file failed o:" << offset_into_file + e.data_offset << "\n";
			}
		}
	} else {
		assert(false && "unhandled case");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_send_data& e) {
	if (!_sending_transfers.count(combine_ids(e.group_number, e.peer_number))) {
		return false;
	}

	auto& peer = _sending_transfers.at(combine_ids(e.group_number, e.peer_number));

	if (!peer.count(e.transfer_id)) {
		return false;
	}

	auto& transfer = peer.at(e.transfer_id);
	transfer.time_since_activity = 0.f;
	if (std::holds_alternative<SendingTransfer::Info>(transfer.v)) {
		auto& info_transfer = std::get<SendingTransfer::Info>(transfer.v);
		for (size_t i = 0; i < e.data_size && (i + e.data_offset) < info_transfer.info_data.size(); i++) {
			e.data[i] = info_transfer.info_data[i + e.data_offset];
		}

		if (e.data_offset + e.data_size >= info_transfer.info_data.size()) {
			// was last read (probably TODO: add transfer destruction event)
			peer.erase(e.transfer_id);
		}
	} else if (std::holds_alternative<SendingTransfer::Chunk>(transfer.v)) {
		auto& chunk_transfer = std::get<SendingTransfer::Chunk>(transfer.v);
		const auto& info = chunk_transfer.content.get<Components::FT1InfoSHA1>();
		// TODO: should we really use file?
		const auto data = chunk_transfer.content.get<Message::Components::Transfer::File>()->read(
			e.data_size,
			(chunk_transfer.chunk_index * uint64_t(info.chunk_size)) + e.data_offset
		);

		// TODO: optimize
		for (size_t i = 0; i < e.data_size && i < data.size; i++) {
			e.data[i] = data[i];
		}

		chunk_transfer.content.get_or_emplace<Message::Components::Transfer::BytesSent>().total += data.size;
		// TODO: add event to propergate to messages
		//_rmm.throwEventUpdate(transfer); // should we?

		//if (e.data_offset + e.data_size >= *insert chunk size here*) {
			//// was last read (probably TODO: add transfer destruction event)
			//peer.erase(e.transfer_id);
		//}
	} else {
		assert(false && "not implemented?");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_done& e) {
	if (!_receiving_transfers.containsPeerTransfer(e.group_number, e.peer_number, e.transfer_id)) {
		return false;
	}

	auto& transfer = _receiving_transfers.getTransfer(e.group_number, e.peer_number, e.transfer_id);

	if (transfer.isInfo()) {
		auto& info = transfer.getInfo();
		auto o = info.content;

		if (o.any_of<Components::FT1InfoSHA1, Components::FT1InfoSHA1Data>()) {
			// we already have the info, discard
			_receiving_transfers.removePeerTransfer(e.group_number, e.peer_number, e.transfer_id);
			return true;
		}

		// check if data matches hash
		auto hash = hash_sha1(info.info_data.data(), info.info_data.size());

		assert(o.all_of<Components::FT1InfoSHA1Hash>());
		if (o.get<Components::FT1InfoSHA1Hash>().hash != hash) {
			std::cerr << "SHA1_NGCFT1 error: got info data mismatching its hash\n";
			// TODO: requeue info request; eg manipulate o.get<Components::ReRequestInfoTimer>();
			_receiving_transfers.removePeerTransfer(e.group_number, e.peer_number, e.transfer_id);
			return true;
		}

		const auto& info_data = o.emplace_or_replace<Components::FT1InfoSHA1Data>(std::move(info.info_data)).data;
		auto& ft_info = o.emplace_or_replace<Components::FT1InfoSHA1>();
		ft_info.fromBuffer(info_data);

		{ // file info
			// TODO: not overwrite fi? since same?
			auto& file_info = o.emplace_or_replace<Message::Components::Transfer::FileInfo>();
			file_info.file_list.emplace_back() = {ft_info.file_name, ft_info.file_size};
			file_info.total_size = ft_info.file_size;
		}

		std::cout << "SHA1_NGCFT1: got info for [" << SHA1Digest{hash} << "]\n" << ft_info << "\n";

		o.remove<Components::ReRequestInfoTimer>();
		if (auto it = std::find(_queue_content_want_info.begin(), _queue_content_want_info.end(), o); it != _queue_content_want_info.end()) {
			_queue_content_want_info.erase(it);
		}

		o.emplace_or_replace<Message::Components::Transfer::TagPaused>();

		updateMessages(o);
	} else if (transfer.isChunk()) {
		auto o = transfer.getChunk().content;
		const auto& info = o.get<Components::FT1InfoSHA1>();
		auto& cc = o.get<Components::FT1ChunkSHA1Cache>();

		// HACK: only check first chunk (they *should* all be the same)
		const auto chunk_index = transfer.getChunk().chunk_indices.front();
		const uint64_t offset_into_file = chunk_index * uint64_t(info.chunk_size);

		assert(chunk_index < info.chunks.size());
		const auto chunk_size = info.chunkSize(chunk_index);
		assert(offset_into_file+chunk_size <= info.file_size);

		const auto chunk_data = o.get<Message::Components::Transfer::File>()->read(chunk_size, offset_into_file);
		assert(!chunk_data.empty());

		// check hash of chunk
		auto got_hash = hash_sha1(chunk_data.ptr, chunk_data.size);
		if (info.chunks.at(chunk_index) == got_hash) {
			std::cout << "SHA1_NGCFT1: got chunk [" << SHA1Digest{got_hash} << "]\n";

			if (!cc.have_all) {
				for (const auto inner_chunk_index : transfer.getChunk().chunk_indices) {
					if (!cc.have_all && !cc.have_chunk[inner_chunk_index]) {
						cc.have_chunk.set(inner_chunk_index);
						cc.have_count += 1;
						if (cc.have_count == info.chunks.size()) {
							// debug check
							for ([[maybe_unused]] size_t i = 0; i < info.chunks.size(); i++) {
								assert(cc.have_chunk[i]);
							}

							cc.have_all = true;
							cc.have_chunk = BitSet(0); // not wasting memory
							std::cout << "SHA1_NGCFT1: got all chunks for \n" << info << "\n";

							// HACK: remap file, to clear ram

							// TODO: error checking
							o.get<Message::Components::Transfer::File>() = std::make_unique<File2RWMapped>(
								o.get<Message::Components::Transfer::FileInfoLocal>().file_list.front(),
								info.file_size
							);
						}

						// good chunk
						// TODO: have wasted + metadata
						o.get_or_emplace<Message::Components::Transfer::BytesReceived>().total += chunk_data.size;
					}
				}

				// queue chunk have for all participants
				// HACK: send immediatly to all participants
				for (const auto c_part : o.get<Components::SuspectedParticipants>().participants) {
					if (!_cr.all_of<Contact::Components::ToxGroupPeerEphemeral>(c_part)) {
						continue;
					}

					const auto [part_group_number, part_peer_number] = _cr.get<Contact::Components::ToxGroupPeerEphemeral>(c_part);

					const auto& info_hash = o.get<Components::FT1InfoSHA1Hash>().hash;

					// convert size_t to uint32_t
					const std::vector<uint32_t> chunk_indices {
						transfer.getChunk().chunk_indices.cbegin(),
						transfer.getChunk().chunk_indices.cend()
					};

					_neep.send_ft1_have(
						part_group_number, part_peer_number,
						static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO),
						info_hash.data(), info_hash.size(),
						chunk_indices.data(), chunk_indices.size()
					);
				}

				if (!cc.have_all) { // debug print self have set
					std::cout << "DEBUG print have bitset: s:" << cc.have_chunk.size_bits();
					for (size_t i = 0; i < cc.have_chunk.size_bytes(); i++) {
						if (i % 16 == 0) {
							std::cout << "\n";
						}
						std::cout << std::hex << (uint16_t)cc.have_chunk.data()[i] << " ";
					}
					std::cout << std::dec << "\n";
				}
			} else {
				std::cout << "SHA1_NGCFT1 warning: got chunk duplicate\n";
			}
		} else {
			// bad chunk
			std::cout << "SHA1_NGCFT1: got BAD chunk from " << e.group_number << ":" << e.peer_number << " [" << info.chunks.at(chunk_index) << "] ; instead got [" << SHA1Digest{got_hash} << "]\n";
		}

		// remove from requested
		// TODO: remove at init and track running transfers differently
		for (const auto it : transfer.getChunk().chunk_indices) {
			o.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks.erase(it);
		}

		updateMessages(o); // mostly for received bytes
	}

	_receiving_transfers.removePeerTransfer(e.group_number, e.peer_number, e.transfer_id);

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_send_done& e) {
	if (!_sending_transfers.count(combine_ids(e.group_number, e.peer_number))) {
		return false;
	}

	auto& peer_transfers = _sending_transfers.at(combine_ids(e.group_number, e.peer_number));
	if (!peer_transfers.count(e.transfer_id)) {
		return false;
	}

	const auto& tv = peer_transfers[e.transfer_id].v;
	if (std::holds_alternative<SendingTransfer::Chunk>(tv)) {
		updateMessages(std::get<SendingTransfer::Chunk>(tv).content); // mostly for sent bytes
	}
	peer_transfers.erase(e.transfer_id);

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_message& e) {
	if (e.file_kind != NGCFT1_file_kind::HASH_SHA1_INFO) {
		return false;
	}

	uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	const auto self_c = c.get<Contact::Components::Self>().self;

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		std::cerr << "SHA1_NGCFT1 error: cant find reg\n";
		return false;
	}

	Message3Registry& reg = *reg_ptr;
	// TODO: check for existence, hs or other syncing mechanics might have sent it already (or like, it arrived 2x or whatever)
	// TODO: use the message dup test provided via rmm
	auto new_msg_e = reg.create();

	{ // contact
		// from
		reg.emplace<Message::Components::ContactFrom>(new_msg_e, c);

		// to
		reg.emplace<Message::Components::ContactTo>(new_msg_e, c.get<Contact::Components::Parent>().parent);
	}

	reg.emplace<Message::Components::ToxGroupMessageID>(new_msg_e, e.message_id);

	reg.emplace<Message::Components::Transfer::TagReceiving>(new_msg_e); // add sending?

	reg.emplace<Message::Components::TimestampProcessed>(new_msg_e, ts);
	//reg.emplace<Components::TimestampWritten>(new_msg_e, 0);
	reg.emplace<Message::Components::Timestamp>(new_msg_e, ts); // reactive?

	reg.emplace<Message::Components::TagUnread>(new_msg_e);

	{ // by whom
		reg.get_or_emplace<Message::Components::SyncedBy>(new_msg_e).ts.try_emplace(self_c, ts);
	}

	{ // we received it, so we have it
		auto& rb = reg.get_or_emplace<Message::Components::ReceivedBy>(new_msg_e).ts;
		rb.try_emplace(c, ts);
		// TODO: how do we handle partial files???
		// tox ft rn only sets self if the file was received fully
		rb.try_emplace(self_c, ts);
	}

	// check if content exists
	const auto sha1_info_hash = std::vector<uint8_t>{e.file_id, e.file_id+e.file_id_size};
	ObjectHandle ce;
	if (_info_to_content.count(sha1_info_hash)) {
		ce = _info_to_content.at(sha1_info_hash);
		std::cout << "SHA1_NGCFT1: new message has existing content\n";
	} else {
		// TODO: backend
		ce = {_os.registry(), _os.registry().create()};
		_info_to_content[sha1_info_hash] = ce;
		std::cout << "SHA1_NGCFT1: new message has new content\n";

		//ce.emplace<Components::FT1InfoSHA1>(sha1_info);
		//ce.emplace<Components::FT1InfoSHA1Data>(sha1_info_data); // keep around? or file?
		ce.emplace<Components::FT1InfoSHA1Hash>(sha1_info_hash);
		//{ // lookup tables and have
			//auto& cc = ce.emplace<Components::FT1ChunkSHA1Cache>();
			//cc.have_all = true;
			//// skip have vec, since all
			////cc.have_chunk
			//cc.have_count = sha1_info.chunks.size(); // need?

			//_info_to_content[sha1_info_hash] = ce;
			//for (size_t i = 0; i < sha1_info.chunks.size(); i++) {
				//_chunks[sha1_info.chunks[i]] = ce;
				//cc.chunk_hash_to_index[sha1_info.chunks[i]] = i;
			//}
		//}

		// TODO: ft1 specific comp
		//ce.emplace<Message::Components::Transfer::File>(std::move(file_impl));
	}
	ce.get_or_emplace<Components::Messages>().messages.push_back({reg, new_msg_e});
	reg_ptr->emplace<Message::Components::Content>(new_msg_e, ce);

	// HACK: assume the message sender is participating. usually a safe bet.
	addParticipation(c, ce);

	// HACK: assume the message sender has all
	ce.get_or_emplace<Components::RemoteHave>().others[c] = {true, {}};

	if (!ce.all_of<Components::ReRequestInfoTimer>() && !ce.all_of<Components::FT1InfoSHA1>()) {
		// TODO: check if already receiving
		_queue_content_want_info.push_back(ce);
	}

	// TODO: queue info dl

	//reg_ptr->emplace<Components::FT1InfoSHA1>(e, sha1_info);
	//reg_ptr->emplace<Components::FT1InfoSHA1Data>(e, sha1_info_data); // keep around? or file?
	//reg.emplace<Components::FT1InfoSHA1Hash>(new_msg_e, std::vector<uint8_t>{e.file_id, e.file_id+e.file_id_size});

	if (auto* cc = ce.try_get<Components::FT1ChunkSHA1Cache>(); cc != nullptr && cc->have_all) {
		reg_ptr->emplace<Message::Components::Transfer::TagHaveAll>(new_msg_e);
	}

	if (ce.all_of<Message::Components::Transfer::FileInfo>()) {
		reg_ptr->emplace<Message::Components::Transfer::FileInfo>(new_msg_e, ce.get<Message::Components::Transfer::FileInfo>());
	}
	if (ce.all_of<Message::Components::Transfer::FileInfoLocal>()) {
		reg_ptr->emplace<Message::Components::Transfer::FileInfoLocal>(new_msg_e, ce.get<Message::Components::Transfer::FileInfoLocal>());
	}
	if (ce.all_of<Message::Components::Transfer::BytesSent>()) {
		reg_ptr->emplace<Message::Components::Transfer::BytesSent>(new_msg_e, ce.get<Message::Components::Transfer::BytesSent>());
	}

	// TODO: queue info/check if we already have info

	_rmm.throwEventConstruct(reg, new_msg_e);

	return true; // false?
}

bool SHA1_NGCFT1::sendFilePath(const Contact3 c, std::string_view file_name, std::string_view file_path) {
	if (
		// TODO: add support of offline queuing
		!_cr.all_of<Contact::Components::ToxGroupEphemeral>(c)
	) {
		return false;
	}

	std::cout << "SHA1_NGCFT1: got sendFilePath()\n";

	auto* reg_ptr = _rmm.get(c);
	if (reg_ptr == nullptr) {
		return false;
	}

	// get current time unix epoch utc
	uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	std::thread(std::move([
		// copy everything
		self = this,
		ts,
		c,
		reg_ptr,
		file_name_ = std::string(file_name),
		file_path_ = std::string(file_path)
	]() mutable {
		auto file_impl = std::make_unique<File2RWMapped>(file_path_, -1);
		if (!file_impl->isGood()) {
			{
				std::lock_guard l{self->_info_builder_queue_mutex};
				self->_info_builder_queue.push_back([file_path_](){
					// back on iterate thread

					std::cerr << "SHA1_NGCFT1 error: failed opening file '" << file_path_ << "'!\n";
				});
				self->_info_builder_dirty = true; // still in scope, set before mutex unlock
			}
			return;
		}

		// 1. build info by hashing all chunks

		FT1InfoSHA1 sha1_info;
		// build info
		sha1_info.file_name = file_name_;
		sha1_info.file_size = file_impl->_file_size; // TODO: remove the reliance on implementation details

		{ // build chunks
			// HACK: load file fully
			// ... its only a hack if its not memory mapped, but reading in chunk_sized chunks is probably a good idea anyway
			const auto file_data = file_impl->read(file_impl->_file_size, 0);
			size_t i = 0;
			for (; i + sha1_info.chunk_size < file_data.size; i += sha1_info.chunk_size) {
				sha1_info.chunks.push_back(hash_sha1(file_data.ptr+i, sha1_info.chunk_size));
			}

			if (i < file_data.size) {
				sha1_info.chunks.push_back(hash_sha1(file_data.ptr+i, file_data.size-i));
			}
		}

		file_impl.reset();

		{
			std::lock_guard l{self->_info_builder_queue_mutex};
			self->_info_builder_queue.push_back(std::move([
				self,
				ts,
				c,
				reg_ptr,
				file_name_,
				file_path_,
				sha1_info = std::move(sha1_info)
			]() mutable { //
				// back on iterate thread

				auto file_impl = std::make_unique<File2RWMapped>(file_path_, sha1_info.file_size);
				if (!file_impl->isGood()) {
					std::cerr << "SHA1_NGCFT1 error: failed opening file '" << file_path_ << "'!\n";
					return;
				}

				// 2. hash info
				std::vector<uint8_t> sha1_info_data;
				std::vector<uint8_t> sha1_info_hash;

				std::cout << "SHA1_NGCFT1 info is: \n" << sha1_info;
				sha1_info_data = sha1_info.toBuffer();
				std::cout << "SHA1_NGCFT1 sha1_info size: " << sha1_info_data.size() << "\n";
				sha1_info_hash = hash_sha1(sha1_info_data.data(), sha1_info_data.size());
				std::cout << "SHA1_NGCFT1 sha1_info_hash: " << bin2hex(sha1_info_hash) << "\n";

				// check if content exists
				ObjectHandle ce;
				if (self->_info_to_content.count(sha1_info_hash)) {
					ce = self->_info_to_content.at(sha1_info_hash);

					// TODO: check if content is incomplete and use file instead
					if (!ce.all_of<Components::FT1InfoSHA1>()) {
						ce.emplace<Components::FT1InfoSHA1>(sha1_info);
					}
					if (!ce.all_of<Components::FT1InfoSHA1Data>()) {
						ce.emplace<Components::FT1InfoSHA1Data>(sha1_info_data);
					}

					// hash has to be set already
					// Components::FT1InfoSHA1Hash

					{ // lookup tables and have
						auto& cc = ce.get_or_emplace<Components::FT1ChunkSHA1Cache>();
						cc.have_all = true;
						// skip have vec, since all
						//cc.have_chunk
						cc.have_count = sha1_info.chunks.size(); // need?

						self->_info_to_content[sha1_info_hash] = ce;
						cc.chunk_hash_to_index.clear(); // for cpy pst
						for (size_t i = 0; i < sha1_info.chunks.size(); i++) {
							self->_chunks[sha1_info.chunks[i]] = ce;
							cc.chunk_hash_to_index[sha1_info.chunks[i]].push_back(i);
						}
					}

					{ // file info
						// TODO: not overwrite fi? since same?
						auto& file_info = ce.emplace_or_replace<Message::Components::Transfer::FileInfo>();
						file_info.file_list.emplace_back() = {std::string{file_name_}, file_impl->_file_size};
						file_info.total_size = file_impl->_file_size;

						ce.emplace_or_replace<Message::Components::Transfer::FileInfoLocal>(std::vector{std::string{file_path_}});
					}

					// cleanup file
					if (ce.all_of<Message::Components::Transfer::File>()) {
						// replace
						ce.remove<Message::Components::Transfer::File>();
					}
					ce.emplace<Message::Components::Transfer::File>(std::move(file_impl));

					if (!ce.all_of<Message::Components::Transfer::BytesSent>()) {
						ce.emplace<Message::Components::Transfer::BytesSent>(0u);
					}

					ce.remove<Message::Components::Transfer::TagPaused>();

					// we dont want the info anymore
					ce.remove<Components::ReRequestInfoTimer>();
					if (auto it = std::find(self->_queue_content_want_info.begin(), self->_queue_content_want_info.end(), ce); it != self->_queue_content_want_info.end()) {
						self->_queue_content_want_info.erase(it);
					}

					// TODO: we dont want chunks anymore

					// TODO: make sure to abort every receiving transfer (sending info and chunk should be fine, info uses copy and chunk handle)
					auto it = self->_queue_content_want_chunk.begin();
					while (
						it != self->_queue_content_want_chunk.end() &&
						(it = std::find(it, self->_queue_content_want_chunk.end(), ce)) != self->_queue_content_want_chunk.end()
					) {
						it = self->_queue_content_want_chunk.erase(it);
					}
				} else {
					// TODO: backend
					ce = {self->_os.registry(), self->_os.registry().create()};
					self->_info_to_content[sha1_info_hash] = ce;

					ce.emplace<Components::FT1InfoSHA1>(sha1_info);
					ce.emplace<Components::FT1InfoSHA1Data>(sha1_info_data); // keep around? or file?
					ce.emplace<Components::FT1InfoSHA1Hash>(sha1_info_hash);
					{ // lookup tables and have
						auto& cc = ce.emplace<Components::FT1ChunkSHA1Cache>();
						cc.have_all = true;
						// skip have vec, since all
						//cc.have_chunk
						cc.have_count = sha1_info.chunks.size(); // need?

						self->_info_to_content[sha1_info_hash] = ce;
						cc.chunk_hash_to_index.clear(); // for cpy pst
						for (size_t i = 0; i < sha1_info.chunks.size(); i++) {
							self->_chunks[sha1_info.chunks[i]] = ce;
							cc.chunk_hash_to_index[sha1_info.chunks[i]].push_back(i);
						}
					}

					{ // file info
						auto& file_info = ce.emplace<Message::Components::Transfer::FileInfo>();
						//const auto& file = ce.get<Message::Components::Transfer::File>();
						file_info.file_list.emplace_back() = {std::string{file_name_}, file_impl->_file_size};
						file_info.total_size = file_impl->_file_size;

						ce.emplace<Message::Components::Transfer::FileInfoLocal>(std::vector{std::string{file_path_}});
					}

					ce.emplace<Message::Components::Transfer::File>(std::move(file_impl));

					ce.emplace<Message::Components::Transfer::BytesSent>(0u);
				}

				const auto c_self = self->_cr.get<Contact::Components::Self>(c).self;
				if (!self->_cr.valid(c_self)) {
					std::cerr << "SHA1_NGCFT1 error: failed to get self!\n";
					return;
				}

				const auto msg_e = reg_ptr->create();
				reg_ptr->emplace<Message::Components::ContactTo>(msg_e, c);
				reg_ptr->emplace<Message::Components::ContactFrom>(msg_e, c_self);
				reg_ptr->emplace<Message::Components::Timestamp>(msg_e, ts); // reactive?
				reg_ptr->emplace<Message::Components::Read>(msg_e, ts);

				reg_ptr->emplace<Message::Components::Transfer::TagHaveAll>(msg_e);
				reg_ptr->emplace<Message::Components::Transfer::TagSending>(msg_e);

				ce.get_or_emplace<Components::Messages>().messages.push_back({*reg_ptr, msg_e});


				//reg_ptr->emplace<Message::Components::Transfer::FileKind>(e, file_kind);
				// file id would be sha1_info hash or something
				//reg_ptr->emplace<Message::Components::Transfer::FileID>(e, file_id);

				// remove? done in updateMessages() anyway
				if (ce.all_of<Message::Components::Transfer::FileInfo>()) {
					reg_ptr->emplace<Message::Components::Transfer::FileInfo>(msg_e, ce.get<Message::Components::Transfer::FileInfo>());
				}
				if (ce.all_of<Message::Components::Transfer::FileInfoLocal>()) {
					reg_ptr->emplace<Message::Components::Transfer::FileInfoLocal>(msg_e, ce.get<Message::Components::Transfer::FileInfoLocal>());
				}
				if (ce.all_of<Message::Components::Transfer::BytesSent>()) {
					reg_ptr->emplace<Message::Components::Transfer::BytesSent>(msg_e, ce.get<Message::Components::Transfer::BytesSent>());
				}

				// TODO: determine if this is true
				//reg_ptr->emplace<Message::Components::Transfer::TagPaused>(e);

				if (self->_cr.any_of<Contact::Components::ToxGroupEphemeral>(c)) {
					const uint32_t group_number = self->_cr.get<Contact::Components::ToxGroupEphemeral>(c).group_number;
					uint32_t message_id = 0;

					// TODO: check return
					self->_nft.NGC_FT1_send_message_public(group_number, message_id, static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO), sha1_info_hash.data(), sha1_info_hash.size());
					reg_ptr->emplace<Message::Components::ToxGroupMessageID>(msg_e, message_id);
				} else if (
					// non online group
					self->_cr.any_of<Contact::Components::ToxGroupPersistent>(c)
				) {
					// create msg_id
					const uint32_t message_id = randombytes_random();
					reg_ptr->emplace<Message::Components::ToxGroupMessageID>(msg_e, message_id);
				}

				reg_ptr->get_or_emplace<Message::Components::SyncedBy>(msg_e).ts.try_emplace(c_self, ts);
				reg_ptr->get_or_emplace<Message::Components::ReceivedBy>(msg_e).ts.try_emplace(c_self, ts);

				self->_rmm.throwEventConstruct(*reg_ptr, msg_e);

				// TODO: place in iterate?
				self->updateMessages(ce);

			}));
			self->_info_builder_dirty = true; // still in scope, set before mutex unlock
		}
	})).detach();

	return true;
}

bool SHA1_NGCFT1::onToxEvent(const Tox_Event_Group_Peer_Exit* e) {
	const auto group_number = tox_event_group_peer_exit_get_group_number(e);
	const auto peer_number = tox_event_group_peer_exit_get_peer_id(e);

	// peer disconnected
	// - remove from all participantions

	auto c = _tcm.getContactGroupPeer(group_number, peer_number);
	if (!static_cast<bool>(c)) {
		return false;
	}

	c.remove<ChunkPicker>();

	for (const auto& [_, o] : _info_to_content) {
		removeParticipation(c, o);

		if (o.all_of<Components::RemoteHave>()) {
			o.get<Components::RemoteHave>().others.erase(c);
		}
	}

	// - clear queues

	for (auto it = _queue_requested_chunk.begin(); it != _queue_requested_chunk.end();) {
		if (group_number == std::get<0>(*it) && peer_number == std::get<1>(*it)) {
			it = _queue_requested_chunk.erase(it);
		} else {
			it++;
		}
	}

	// TODO: nfcft1 should have fired receive/send done events for all them running transfers

	return false;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCEXT_ft1_have& e) {
	std::cerr << "SHA1_NGCFT1: FT1_HAVE s:" << e.chunks.size() << "\n";

	if (e.file_kind != static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO)) {
		return false;
	}

	SHA1Digest info_hash{e.file_id};

	auto itc_it = _info_to_content.find(info_hash);
	if (itc_it == _info_to_content.end()) {
		// we are not interested and dont track this
		return false;
	}

	auto o = itc_it->second;

	if (!static_cast<bool>(o)) {
		std::cerr << "SHA1_NGCFT1 error: tracking info has null object\n";
		return false;
	}

	const size_t num_total_chunks = o.get<Components::FT1InfoSHA1>().chunks.size();

	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);

	// we might not know yet
	addParticipation(c, o);

	auto& remote_have = o.get_or_emplace<Components::RemoteHave>().others;
	if (!remote_have.contains(c)) {
		// init
		remote_have.emplace(c, Components::RemoteHave::Entry{false, num_total_chunks});
	}

	auto& remote_have_peer = remote_have.at(c);
	if (!remote_have_peer.have_all) {
		assert(remote_have_peer.have.size_bits() >= num_total_chunks);

		for (const auto c_i : e.chunks) {
			if (c_i >= num_total_chunks) {
				std::cerr << "SHA1_NGCFT1 error: remote sent have with out-of-range chunk index!!!\n";
				std::cerr << info_hash << ": " << c_i << " >= " << num_total_chunks << "\n";
				continue;
			}

			assert(c_i < num_total_chunks);
			remote_have_peer.have.set(c_i);
		}

		// check for completion?
		// TODO: optimize
		bool test_all {true};
		for (size_t i = 0; i < remote_have_peer.have.size_bits(); i++) {
			if (!remote_have_peer.have[i]) {
				test_all = false;
				break;
			}
		}

		if (test_all) {
			// optimize
			remote_have_peer.have_all = true;
			remote_have_peer.have = BitSet{};
		}
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCEXT_ft1_bitset& e) {
	std::cerr << "SHA1_NGCFT1: FT1_BITSET o:" << e.start_chunk << " s:" << e.chunk_bitset.size() << "\n";

	if (e.file_kind != static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO)) {
		return false;
	}

	if (e.chunk_bitset.empty()) {
		// what
		return false;
	}

	SHA1Digest info_hash{e.file_id};

	auto itc_it = _info_to_content.find(info_hash);
	if (itc_it == _info_to_content.end()) {
		// we are not interested and dont track this
		return false;
	}

	auto o = itc_it->second;

	if (!static_cast<bool>(o)) {
		std::cerr << "SHA1_NGCFT1 error: tracking info has null object\n";
		return false;
	}

	const size_t num_total_chunks = o.get<Components::FT1InfoSHA1>().chunks.size();
	// +1 for byte rounding
	if (num_total_chunks+1 < e.start_chunk + (e.chunk_bitset.size()*8)) {
		std::cerr << "SHA1_NGCFT1 error: got bitset.size+start that is larger then number of chunks!!\n";
		return false;
	}

	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);

	// we might not know yet
	addParticipation(c, o);

	auto& remote_have = o.get_or_emplace<Components::RemoteHave>().others;
	if (!remote_have.contains(c)) {
		// init
		remote_have.emplace(c, Components::RemoteHave::Entry{false, num_total_chunks});
	}

	auto& remote_have_peer = remote_have.at(c);
	if (!remote_have_peer.have_all) { // TODO: maybe unset with bitset?
		BitSet event_bitset{e.chunk_bitset};
		remote_have_peer.have.merge(event_bitset, e.start_chunk);

		// check for completion?
		// TODO: optimize
		bool test_all {true};
		for (size_t i = 0; i < remote_have_peer.have.size_bits(); i++) {
			if (!remote_have_peer.have[i]) {
				test_all = false;
				break;
			}
		}

		if (test_all) {
			// optimize
			remote_have_peer.have_all = true;
			remote_have_peer.have = BitSet{};
		}
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCEXT_pc1_announce& e) {
	std::cerr << "SHA1_NGCFT1: PC1_ANNOUNCE s:" << e.id.size() << "\n";
	// id is file_kind + id
	uint32_t file_kind = 0u;

	static_assert(SHA1Digest{}.size() == 20);
	if (e.id.size() != sizeof(file_kind) + 20) {
		// not for us
		return false;
	}

	for (size_t i = 0; i < sizeof(file_kind); i++) {
		file_kind |= uint32_t(e.id[i]) << (i*8);
	}

	if (file_kind != static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO)) {
		return false;
	}

	SHA1Digest hash{e.id.data()+sizeof(file_kind), 20};

	// if have use hash(-info) for file, add to participants
	std::cout << "SHA1_NGCFT1: got ParticipationChatter1 announce from " << e.group_number << ":" << e.peer_number << " for " << hash << "\n";

	auto itc_it = _info_to_content.find(hash);
	if (itc_it == _info_to_content.end()) {
		// we are not interested and dont track this
		return false;
	}

	// add them to participants
	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	auto o = itc_it->second;
	const bool was_new = addParticipation(c, o);
	if (was_new) {
		std::cout << "SHA1_NGCFT1: and we where interested!\n";
		// we should probably send the bitset back here / add to queue (can be multiple packets)
	}

	return false;
}

