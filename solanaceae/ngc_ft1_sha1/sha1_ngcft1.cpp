#include "./sha1_ngcft1.hpp"

#include <solanaceae/toxcore/utils.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/tox_messages/components.hpp>

#include <solanaceae/message3/file_r_file.hpp>

#include "./ft1_sha1_info.hpp"
#include "./hash_utils.hpp"

#include <sodium.h>

#include <entt/container/dense_set.hpp>

#include "./file_rw_mapped.hpp"

#include <iostream>
#include <variant>
#include <filesystem>
#include <mutex>
#include <future>

namespace Message::Components {

	using Content = ContentHandle;

} // Message::Components

// TODO: rename to content components
namespace Components {

	struct Messages {
		std::vector<Message3Handle> messages;
	};

	using FT1InfoSHA1 = FT1InfoSHA1;

	struct FT1InfoSHA1Data {
		std::vector<uint8_t> data;
	};

	struct FT1InfoSHA1Hash {
		std::vector<uint8_t> hash;
	};

	struct FT1ChunkSHA1Cache {
		std::vector<bool> have_chunk;
		bool have_all {false};
		size_t have_count {0};
		entt::dense_map<SHA1Digest, std::vector<size_t>> chunk_hash_to_index;

		std::vector<size_t> chunkIndices(const SHA1Digest& hash) const;
		bool haveChunk(const SHA1Digest& hash) const;
	};

	struct FT1ChunkSHA1Requested {
		// requested chunks with a timer since last request
		entt::dense_map<size_t, float> chunks;
	};

	struct SuspectedParticipants {
		entt::dense_set<Contact3> participants;
	};

	struct ReRequestInfoTimer {
		float timer {0.f};
	};

	struct ReadHeadHint {
		// points to the first byte we want
		// this is just a hint, that can be set from outside
		// to guide the sequential "piece picker" strategy
		// the strategy *should* set this to the first byte we dont yet have
		uint64_t offset_into_file {0u};
	};

} // Components

std::vector<size_t> Components::FT1ChunkSHA1Cache::chunkIndices(const SHA1Digest& hash) const {
	const auto it = chunk_hash_to_index.find(hash);
	if (it != chunk_hash_to_index.cend()) {
		return it->second;
	} else {
		return {};
	}
}

bool Components::FT1ChunkSHA1Cache::haveChunk(const SHA1Digest& hash) const {
	if (have_all) { // short cut
		return true;
	}

	if (auto i_vec = chunkIndices(hash); !i_vec.empty()) {
		// TODO: should i test all?
		return have_chunk[i_vec.front()];
	}

	// not part of this file
	return false;
}

static size_t chunkSize(const FT1InfoSHA1& sha1_info, size_t chunk_index) {
	if (chunk_index+1 == sha1_info.chunks.size()) {
		// last chunk
		return sha1_info.file_size - chunk_index * sha1_info.chunk_size;
	} else {
		return sha1_info.chunk_size;
	}
}

void SHA1_NGCFT1::queueUpRequestChunk(uint32_t group_number, uint32_t peer_number, ContentHandle content, const SHA1Digest& hash) {
	for (auto& [i_g, i_p, i_m, i_h, i_t] : _queue_requested_chunk) {
		// if already in queue
		if (i_g == group_number && i_p == peer_number && i_h == hash) {
			// update timer
			i_t = 0.f;
			return;
		}
	}

	// check for running transfer
	if (_sending_transfers.count(combineIds(group_number, peer_number))) {
		for (const auto& [_, transfer] : _sending_transfers.at(combineIds(group_number, peer_number))) {
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

uint64_t SHA1_NGCFT1::combineIds(const uint32_t group_number, const uint32_t peer_number) {
	return (uint64_t(group_number) << 32) | peer_number;
}

void SHA1_NGCFT1::updateMessages(ContentHandle ce) {
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

std::optional<std::pair<uint32_t, uint32_t>> SHA1_NGCFT1::selectPeerForRequest(ContentHandle ce) {
	// get a list of peers we can request this file from
	// TODO: randomly request from non SuspectedParticipants
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

	// 1 in 20 chance to ask random peer instead
	// TODO: config + tweak
	// TODO: save group in content to avoid the tox_peers list build
	if (tox_peers.empty() || (_rng()%20) == 0) {
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
	Contact3Registry& cr,
	RegistryMessageModel& rmm,
	NGCFT1& nft,
	ToxContactModel2& tcm
) :
	_cr(cr),
	_rmm(rmm),
	_nft(nft),
	_tcm(tcm)
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

				// if we have not heard for 10sec, timeout
				if (it->second.time_since_activity >= 10.f) {
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
		for (auto peer_it = _receiving_transfers.begin(); peer_it != _receiving_transfers.end();) {
			for (auto it = peer_it->second.begin(); it != peer_it->second.end();) {
				it->second.time_since_activity += delta;

				// if we have not heard for 10sec, timeout
				if (it->second.time_since_activity >= 10.f) {
					std::cerr << "SHA1_NGCFT1 warning: receiving tansfer timed out " << "." << int(it->first) << "\n";
					// TODO: if info, requeue? or just keep the timer comp? - no, timer comp will continue ticking, even if loading
					//it->second.v
					it = peer_it->second.erase(it);
				} else {
					it++;
				}
			}

			if (peer_it->second.empty()) {
				// cleanup unused peers too agressive?
				peer_it = _receiving_transfers.erase(peer_it);
			} else {
				peer_it++;
			}
		}

		// queued requests
		for (auto it = _queue_requested_chunk.begin(); it != _queue_requested_chunk.end();) {
			float& timer = std::get<float>(*it);
			timer += delta;

			if (timer >= 10.f) {
				it = _queue_requested_chunk.erase(it);
			} else {
				it++;
			}
		}

		{ // requested info timers
			std::vector<Content> timed_out;
			_contentr.view<Components::ReRequestInfoTimer>().each([delta, &timed_out](Content e, Components::ReRequestInfoTimer& rrit) {
				rrit.timer += delta;

				// 15sec, TODO: config
				if (rrit.timer >= 15.f) {
					timed_out.push_back(e);
				}
			});
			for (const auto e : timed_out) {
				// TODO: avoid dups
				_queue_content_want_info.push_back({_contentr, e});
				_contentr.remove<Components::ReRequestInfoTimer>(e);
			}
		}
		{ // requested chunk timers
			_contentr.view<Components::FT1ChunkSHA1Requested>().each([delta](Components::FT1ChunkSHA1Requested& ftchunk_requested) {
				for (auto it = ftchunk_requested.chunks.begin(); it != ftchunk_requested.chunks.end();) {
					it->second += delta;

					// 20sec, TODO: config
					if (it->second >= 20.f) {
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
	size_t running_receiving_transfer_count {0};
	for (const auto& [_, transfers] : _receiving_transfers) {
		running_receiving_transfer_count += transfers.size();
	}

	if (running_sending_transfer_count < _max_concurrent_out) {
		// TODO: for each peer? transfer cap per peer?
		// TODO: info queue
		if (!_queue_requested_chunk.empty()) { // then check for chunk requests
			const auto [group_number, peer_number, ce, chunk_hash, _] = _queue_requested_chunk.front();

			auto chunk_idx_vec = ce.get<Components::FT1ChunkSHA1Cache>().chunkIndices(chunk_hash);
			if (!chunk_idx_vec.empty()) {

				// check if already sending
				bool already_sending_to_this_peer = false;
				if (_sending_transfers.count(combineIds(group_number, peer_number))) {
					for (const auto& [_2, t] : _sending_transfers.at(combineIds(group_number, peer_number))) {
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
							[combineIds(group_number, peer_number)]
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

					// naive, choose first chunk we dont have (double requests!!)
					for (size_t chunk_idx = 0; chunk_idx < cc.have_chunk.size(); chunk_idx++) {
						if (cc.have_chunk[chunk_idx]) {
							continue;
						}

						// check by hash
						if (cc.haveChunk(info.chunks.at(chunk_idx))) {
							// TODO: fix this, a completed chunk should fill all the indecies it occupies
							cc.have_chunk[chunk_idx] = true;
							cc.have_count += 1;
							if (cc.have_count == info.chunks.size()) {
								cc.have_all = true;
								cc.have_chunk.clear();
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
	// TODO: ensure dir exists
	if (full_file_path.back() != '/') {
		full_file_path += "/";
	}

	std::filesystem::create_directories(full_file_path);

	const auto& info = ce.get<Components::FT1InfoSHA1>();
	full_file_path += info.file_name;

	ce.emplace<Message::Components::Transfer::FileInfoLocal>(std::vector{full_file_path});

	std::unique_ptr<FileRWMapped> file_impl;
	const bool file_exists = std::filesystem::exists(full_file_path);

	file_impl = std::make_unique<FileRWMapped>(full_file_path, info.file_size);

	if (!file_impl->isGood()) {
		std::cerr << "SHA1_NGCFT1 error: failed opening file '" << full_file_path << "'!\n";
		//e.e.remove<Message::Components::Transfer::ActionAccept>(); // stop
		return false;
	}

	{ // next, create chuck cache and check for existing data
		auto& cc = ce.emplace<Components::FT1ChunkSHA1Cache>();
		auto& bytes_received = ce.get_or_emplace<Message::Components::Transfer::BytesReceived>().total;
		cc.have_all = false;
		cc.have_count = 0;

		cc.chunk_hash_to_index.clear(); // if copy pasta

		if (file_exists) {
			// iterate existing file
			for (size_t i = 0; i < info.chunks.size(); i++) {
				const uint64_t chunk_size = info.chunkSize(i);
				auto existing_data = file_impl->read(i*uint64_t(info.chunk_size), chunk_size);
				assert(existing_data.size() == chunk_size);

				// TODO: avoid copy

				const auto data_hash = SHA1Digest{hash_sha1(existing_data.data(), existing_data.size())};
				const bool data_equal = data_hash == info.chunks.at(i);

				cc.have_chunk.push_back(data_equal);

				if (data_equal) {
					cc.have_count += 1;
					bytes_received += chunk_size;
					//std::cout << "existing i[" << info.chunks.at(i) << "] == d[" << data_hash << "]\n";
				} else {
					//std::cout << "unk i[" << info.chunks.at(i) << "] != d[" << data_hash << "]\n";
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
				cc.have_chunk.push_back(false);
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
			[combineIds(e.group_number, e.peer_number)]
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

		auto ce = _chunks.at(chunk_hash);

		{ // they advertise interest in the content
			const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
			ce.get_or_emplace<Components::SuspectedParticipants>().participants.emplace(c);
		}

		assert(ce.all_of<Components::FT1ChunkSHA1Cache>());

		if (!ce.get<Components::FT1ChunkSHA1Cache>().haveChunk(chunk_hash)) {
			// we dont have the chunk
			return false;
		}

		// queue good request
		queueUpRequestChunk(e.group_number, e.peer_number, ce, chunk_hash);
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

		_receiving_transfers
			[combineIds(e.group_number, e.peer_number)]
			[e.transfer_id]
				.v = ReceivingTransfer::Info{ce, std::vector<uint8_t>(e.file_size)};

		e.accept = true;
	} else if (e.file_kind == NGCFT1_file_kind::HASH_SHA1_CHUNK) {
		SHA1Digest sha1_chunk_hash {e.file_id, e.file_id_size};

		if (!_chunks.count(sha1_chunk_hash)) {
			// no idea about this content
			return false;
		}

		auto ce = _chunks.at(sha1_chunk_hash);

		// CHECK IF TRANSFER IN PROGESS!!

		{ // they have the content (probably, might be fake, should move this to done)
			const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
			ce.get_or_emplace<Components::SuspectedParticipants>().participants.emplace(c);
		}

		assert(ce.all_of<Components::FT1InfoSHA1>());
		assert(ce.all_of<Components::FT1ChunkSHA1Cache>());

		const auto& cc = ce.get<Components::FT1ChunkSHA1Cache>();
		if (cc.haveChunk(sha1_chunk_hash)) {
			std::cout << "SHA1_NGCFT1: chunk rejected, already have [" << SHA1Digest{sha1_chunk_hash} << "]\n";
			// we have the chunk
			return false;
		}
		// TODO: cache position

		// calc offset_into_file
		auto idx_vec = cc.chunkIndices(sha1_chunk_hash);
		assert(!idx_vec.empty());

		const auto& info = ce.get<Components::FT1InfoSHA1>();

		// TODO: check e.file_size
		assert(e.file_size == info.chunkSize(idx_vec.front()));

		_receiving_transfers
			[combineIds(e.group_number, e.peer_number)]
			[e.transfer_id]
				.v = ReceivingTransfer::Chunk{ce, idx_vec};

		e.accept = true;

		std::cout << "SHA1_NGCFT1: accepted chunk [" << SHA1Digest{sha1_chunk_hash} << "]\n";
	} else {
		assert(false && "unhandled case");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_data& e) {
	if (!_receiving_transfers.count(combineIds(e.group_number, e.peer_number))) {
		return false;
	}

	auto& peer_transfers = _receiving_transfers.at(combineIds(e.group_number, e.peer_number));
	if (!peer_transfers.count(e.transfer_id)) {
		return false;
	}

	auto& tv = peer_transfers[e.transfer_id].v;
	peer_transfers[e.transfer_id].time_since_activity = 0.f;
	if (std::holds_alternative<ReceivingTransfer::Info>(tv)) {
		auto& info_data = std::get<ReceivingTransfer::Info>(tv).info_data;
		for (size_t i = 0; i < e.data_size && i + e.data_offset < info_data.size(); i++) {
			info_data[i+e.data_offset] = e.data[i];
		}
	} else if (std::holds_alternative<ReceivingTransfer::Chunk>(tv)) {
		auto ce = std::get<ReceivingTransfer::Chunk>(tv).content;

		assert(ce.all_of<Message::Components::Transfer::File>());
		auto* file = ce.get<Message::Components::Transfer::File>().get();
		assert(file != nullptr);

		for (const auto chunk_index : std::get<ReceivingTransfer::Chunk>(tv).chunk_indices) {
			const auto offset_into_file = chunk_index* ce.get<Components::FT1InfoSHA1>().chunk_size;

			// TODO: avoid temporary copy
			// TODO: check return
			if (!file->write(offset_into_file + e.data_offset, {e.data, e.data + e.data_size})) {
				std::cerr << "SHA1_NGCFT1 error: writing file failed o:" << offset_into_file + e.data_offset << "\n";
			}
		}
	} else {
		assert(false && "unhandled case");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_send_data& e) {
	if (!_sending_transfers.count(combineIds(e.group_number, e.peer_number))) {
		return false;
	}

	auto& peer = _sending_transfers.at(combineIds(e.group_number, e.peer_number));

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
		const auto data = chunk_transfer.content.get<Message::Components::Transfer::File>()->read((chunk_transfer.chunk_index * uint64_t(info.chunk_size)) + e.data_offset, e.data_size);

		// TODO: optimize
		for (size_t i = 0; i < e.data_size && i < data.size(); i++) {
			e.data[i] = data[i];
		}

		chunk_transfer.content.get_or_emplace<Message::Components::Transfer::BytesSent>().total += data.size();
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
	if (!_receiving_transfers.count(combineIds(e.group_number, e.peer_number))) {
		return false;
	}

	auto& peer_transfers = _receiving_transfers.at(combineIds(e.group_number, e.peer_number));
	if (!peer_transfers.count(e.transfer_id)) {
		return false;
	}

	const auto& tv = peer_transfers[e.transfer_id].v;
	if (std::holds_alternative<ReceivingTransfer::Info>(tv)) {
		auto& info = std::get<ReceivingTransfer::Info>(tv);
		auto ce = info.content;

		if (ce.any_of<Components::FT1InfoSHA1, Components::FT1InfoSHA1Data>()) {
			// we already have the info, discard
			peer_transfers.erase(e.transfer_id);
			return true;
		}

		// check if data matches hash
		auto hash = hash_sha1(info.info_data.data(), info.info_data.size());

		assert(ce.all_of<Components::FT1InfoSHA1Hash>());
		if (ce.get<Components::FT1InfoSHA1Hash>().hash != hash) {
			std::cerr << "SHA1_NGCFT1 error: got info data mismatching its hash\n";
			// requeue info request
			peer_transfers.erase(e.transfer_id);
			return true;
		}

		const auto& info_data = ce.emplace_or_replace<Components::FT1InfoSHA1Data>(std::move(info.info_data)).data;
		auto& ft_info = ce.emplace_or_replace<Components::FT1InfoSHA1>();
		ft_info.fromBuffer(info_data);

		{ // file info
			// TODO: not overwrite fi? since same?
			auto& file_info = ce.emplace_or_replace<Message::Components::Transfer::FileInfo>();
			file_info.file_list.emplace_back() = {ft_info.file_name, ft_info.file_size};
			file_info.total_size = ft_info.file_size;
		}

		std::cout << "SHA1_NGCFT1: got info for [" << SHA1Digest{hash} << "]\n" << ft_info << "\n";

		ce.remove<Components::ReRequestInfoTimer>();
		if (auto it = std::find(_queue_content_want_info.begin(), _queue_content_want_info.end(), ce); it != _queue_content_want_info.end()) {
			_queue_content_want_info.erase(it);
		}

		ce.emplace_or_replace<Message::Components::Transfer::TagPaused>();

		updateMessages(ce);
	} else if (std::holds_alternative<ReceivingTransfer::Chunk>(tv)) {
		auto ce = std::get<ReceivingTransfer::Chunk>(tv).content;
		const auto& info = ce.get<Components::FT1InfoSHA1>();
		auto& cc = ce.get<Components::FT1ChunkSHA1Cache>();

		// HACK: only check first chunk (they *should* all be the same)
		const auto chunk_index = std::get<ReceivingTransfer::Chunk>(tv).chunk_indices.front();
		const uint64_t offset_into_file = chunk_index * uint64_t(info.chunk_size);

		assert(chunk_index < info.chunks.size());
		const auto chunk_size = info.chunkSize(chunk_index);
		assert(offset_into_file+chunk_size <= info.file_size);

		const auto chunk_data = ce.get<Message::Components::Transfer::File>()->read(offset_into_file, chunk_size);

		// check hash of chunk
		auto got_hash = hash_sha1(chunk_data.data(), chunk_data.size());
		if (info.chunks.at(chunk_index) == got_hash) {
			std::cout << "SHA1_NGCFT1: got chunk [" << SHA1Digest{got_hash} << "]\n";

			if (!cc.have_all) {
				for (const auto inner_chunk_index : std::get<ReceivingTransfer::Chunk>(tv).chunk_indices) {
					if (!cc.have_all && !cc.have_chunk.at(inner_chunk_index)) {
						cc.have_chunk.at(inner_chunk_index) = true;
						cc.have_count += 1;
						if (cc.have_count == info.chunks.size()) {
							// debug check
							for ([[maybe_unused]] const bool it : cc.have_chunk) {
								assert(it);
							}

							cc.have_all = true;
							cc.have_chunk.clear(); // not wasting memory
							std::cout << "SHA1_NGCFT1: got all chunks for \n" << info << "\n";

							// HACK: remap file, to clear ram

							// TODO: error checking
							ce.get<Message::Components::Transfer::File>() = std::make_unique<FileRWMapped>(
								ce.get<Message::Components::Transfer::FileInfoLocal>().file_list.front(),
								info.file_size
							);
						}

						// good chunk
						// TODO: have wasted + metadata
						ce.get_or_emplace<Message::Components::Transfer::BytesReceived>().total += chunk_data.size();
					}
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
		for (const auto it : std::get<ReceivingTransfer::Chunk>(tv).chunk_indices) {
			ce.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks.erase(it);
		}

		updateMessages(ce); // mostly for received bytes
	}

	peer_transfers.erase(e.transfer_id);

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_send_done& e) {
	if (!_sending_transfers.count(combineIds(e.group_number, e.peer_number))) {
		return false;
	}

	auto& peer_transfers = _sending_transfers.at(combineIds(e.group_number, e.peer_number));
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
		auto& synced_by = reg.get_or_emplace<Message::Components::SyncedBy>(new_msg_e).list;
		synced_by.emplace(self_c);
	}

	// check if content exists
	const auto sha1_info_hash = std::vector<uint8_t>{e.file_id, e.file_id+e.file_id_size};
	ContentHandle ce;
	if (_info_to_content.count(sha1_info_hash)) {
		ce = _info_to_content.at(sha1_info_hash);
		std::cout << "SHA1_NGCFT1: new message has existing content\n";
	} else {
		ce = {_contentr, _contentr.create()};
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

	ce.get_or_emplace<Components::SuspectedParticipants>().participants.emplace(c);

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
		// TODO: rw?
		// TODO: memory mapped would be king
		auto file_impl = std::make_unique<FileRFile>(file_path_);
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
		sha1_info.file_size = file_impl->_file_size;

		{ // build chunks
			// HACK: load file fully
			// TODO: the speed is truly horrid
			const auto file_data = file_impl->read(0, file_impl->_file_size);
			size_t i = 0;
			for (; i + sha1_info.chunk_size < file_data.size(); i += sha1_info.chunk_size) {
				sha1_info.chunks.push_back(hash_sha1(file_data.data()+i, sha1_info.chunk_size));
			}

			if (i < file_data.size()) {
				sha1_info.chunks.push_back(hash_sha1(file_data.data()+i, file_data.size()-i));
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

				auto file_impl = std::make_unique<FileRFile>(file_path_);
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
				ContentHandle ce;
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
					ce = {self->_contentr, self->_contentr.create()};
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

					// TODO: generalize?
					auto& synced_by = reg_ptr->emplace<Message::Components::SyncedBy>(msg_e).list;
					synced_by.emplace(c_self);
				} else if (
					// non online group
					self->_cr.any_of<Contact::Components::ToxGroupPersistent>(c)
				) {
					// create msg_id
					const uint32_t message_id = randombytes_random();
					reg_ptr->emplace<Message::Components::ToxGroupMessageID>(msg_e, message_id);

					// TODO: generalize?
					auto& synced_by = reg_ptr->emplace<Message::Components::SyncedBy>(msg_e).list;
					synced_by.emplace(c_self);
				}

				self->_rmm.throwEventConstruct(*reg_ptr, msg_e);

				// TODO: place in iterate?
				self->updateMessages(ce);

			}));
			self->_info_builder_dirty = true; // still in scope, set before mutex unlock
		}
	})).detach();

	return true;
}

