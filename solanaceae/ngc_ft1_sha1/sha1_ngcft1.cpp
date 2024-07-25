#include "./sha1_ngcft1.hpp"

#include <solanaceae/util/utils.hpp>

#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/message3/components.hpp>
#include <solanaceae/tox_messages/components.hpp>
#include <solanaceae/object_store/meta_components_file.hpp>

#include "./util.hpp"

#include "./ft1_sha1_info.hpp"
#include "./hash_utils.hpp"

#include <sodium.h>

#include <entt/container/dense_set.hpp>

#include "./file_constructor.hpp"

#include "./components.hpp"
#include "./contact_components.hpp"
#include "./chunk_picker.hpp"
#include "./participation.hpp"

#include "./re_announce_systems.hpp"
#include "./chunk_picker_systems.hpp"
#include "./transfer_stats_systems.hpp"

#include <iostream>
#include <filesystem>
#include <vector>

static size_t chunkSize(const FT1InfoSHA1& sha1_info, size_t chunk_index) {
	if (chunk_index+1 == sha1_info.chunks.size()) {
		// last chunk
		return sha1_info.file_size - chunk_index * sha1_info.chunk_size;
	} else {
		return sha1_info.chunk_size;
	}
}

void SHA1_NGCFT1::queueUpRequestChunk(uint32_t group_number, uint32_t peer_number, ObjectHandle obj, const SHA1Digest& hash) {
	for (auto& [i_g, i_p, i_o, i_h, i_t] : _queue_requested_chunk) {
		// if already in queue
		if (i_g == group_number && i_p == peer_number && i_h == hash) {
			// update timer
			i_t = 0.f;
			return;
		}
	}

	// check for running transfer
	auto chunk_idx_vec = obj.get<Components::FT1ChunkSHA1Cache>().chunkIndices(hash);
	// list is 1 entry in 99% of cases
	for (const size_t chunk_idx : chunk_idx_vec) {
		if (_sending_transfers.containsPeerChunk(group_number, peer_number, obj, chunk_idx)) {
			// already sending
			return; // skip
		}
	}

	// not in queue yet
	_queue_requested_chunk.push_back(std::make_tuple(group_number, peer_number, obj, hash, 0.f));
}

void SHA1_NGCFT1::updateMessages(ObjectHandle o) {
	assert(o.all_of<Components::Messages>());

	for (auto msg : o.get<Components::Messages>().messages) {
		msg.emplace_or_replace<Message::Components::MessageFileObject>(o);

		// messages no long hold this info
		// this should not update messages anymore but simply just update the object
		// and receivers should listen for object updates (?)

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

void SHA1_NGCFT1::queueBitsetSendFull(Contact3Handle c, ObjectHandle o) {
	if (!static_cast<bool>(c) || !static_cast<bool>(o)) {
		assert(false);
		return;
	}

	// TODO: only queue if not already sent??


	if (!o.all_of<Components::FT1ChunkSHA1Cache, Components::FT1InfoSHA1>()) {
		return;
	}

	_queue_send_bitset.push_back(QBitsetEntry{c, o});
}

File2I* SHA1_NGCFT1::objGetFile2Write(ObjectHandle o) {
	auto* file2_comp_ptr = o.try_get<Components::FT1File2>();
	if (file2_comp_ptr == nullptr || !file2_comp_ptr->file || !file2_comp_ptr->file->can_write || !file2_comp_ptr->file->isGood()) {
		// (re)request file2 from backend
		auto new_file = _mfb.file2(o, StorageBackendI::FILE2_WRITE);
		if (!new_file || !new_file->can_write || !new_file->isGood()) {
			std::cerr << "SHA1_NGCFT1 error: failed to open object for writing\n";
			return nullptr; // early out
		}
		file2_comp_ptr = &o.emplace_or_replace<Components::FT1File2>(std::move(new_file));
	}
	assert(file2_comp_ptr != nullptr);
	assert(static_cast<bool>(file2_comp_ptr->file));

	return file2_comp_ptr->file.get();
}

File2I* SHA1_NGCFT1::objGetFile2Read(ObjectHandle o) {
	auto* file2_comp_ptr = o.try_get<Components::FT1File2>();
	if (file2_comp_ptr == nullptr || !file2_comp_ptr->file || !file2_comp_ptr->file->can_read || !file2_comp_ptr->file->isGood()) {
		// (re)request file2 from backend
		auto new_file = _mfb.file2(o, StorageBackendI::FILE2_READ);
		if (!new_file || !new_file->can_read || !new_file->isGood()) {
			std::cerr << "SHA1_NGCFT1 error: failed to open object for reading\n";
			return nullptr; // early out
		}
		file2_comp_ptr = &o.emplace_or_replace<Components::FT1File2>(std::move(new_file));
	}
	assert(file2_comp_ptr != nullptr);
	assert(static_cast<bool>(file2_comp_ptr->file));

	return file2_comp_ptr->file.get();
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
	_neep(neep),
	_mfb(os)
{
	// TODO: also create and destroy
	//_os.subscribe(this, ObjectStore_Event::object_construct);
	_os.subscribe(this, ObjectStore_Event::object_update);
	//_os.subscribe(this, ObjectStore_Event::object_destroy);

	_nft.subscribe(this, NGCFT1_Event::recv_request);
	_nft.subscribe(this, NGCFT1_Event::recv_init);
	_nft.subscribe(this, NGCFT1_Event::recv_data);
	_nft.subscribe(this, NGCFT1_Event::send_data);
	_nft.subscribe(this, NGCFT1_Event::recv_done);
	_nft.subscribe(this, NGCFT1_Event::send_done);
	_nft.subscribe(this, NGCFT1_Event::recv_message);

	_rmm.subscribe(this, RegistryMessageModel_Event::send_file_path);

	_tep.subscribe(this, Tox_Event_Type::TOX_EVENT_GROUP_PEER_JOIN);
	_tep.subscribe(this, Tox_Event_Type::TOX_EVENT_GROUP_PEER_EXIT);

	_neep.subscribe(this, NGCEXT_Event::FT1_HAVE);
	_neep.subscribe(this, NGCEXT_Event::FT1_BITSET);
	_neep.subscribe(this, NGCEXT_Event::FT1_HAVE_ALL);
	_neep.subscribe(this, NGCEXT_Event::PC1_ANNOUNCE);
}

float SHA1_NGCFT1::iterate(float delta) {
	//std::cerr << "---------- new tick ----------\n";
	_mfb.tick(); // does not need to be called as often, once every sec would be enough, but the pointer deref + atomic bool should be very fast

	entt::dense_map<Contact3, size_t> peer_open_requests;

	{ // timers
		// sending transfers
		_sending_transfers.tick(delta);

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
			_os.registry().view<Components::FT1ChunkSHA1Requested>().each([delta, &peer_open_requests](Components::FT1ChunkSHA1Requested& ftchunk_requested) {
				for (auto it = ftchunk_requested.chunks.begin(); it != ftchunk_requested.chunks.end();) {
					it->second.timer += delta;

					// TODO: config
					if (it->second.timer >= 60.f) {
						it = ftchunk_requested.chunks.erase(it);
					} else {
						peer_open_requests[it->second.c] += 1;
						it++;
					}
				}
			});
		}
	}

	Systems::re_announce(_os.registry(), _cr, _neep, delta);

	{ // send out bitsets
		// currently 1 per tick
		if (!_queue_send_bitset.empty()) {
			const auto& qe = _queue_send_bitset.front();

			if (static_cast<bool>(qe.o) && static_cast<bool>(qe.c) && qe.c.all_of<Contact::Components::ToxGroupPeerEphemeral>() && qe.o.all_of<Components::FT1InfoSHA1, Components::FT1InfoSHA1Hash, Components::FT1ChunkSHA1Cache>()) {
				const auto [group_number, peer_number] = qe.c.get<Contact::Components::ToxGroupPeerEphemeral>();
				const auto& info_hash = qe.o.get<Components::FT1InfoSHA1Hash>().hash;
				const auto& info = qe.o.get<Components::FT1InfoSHA1>();
				const auto total_chunks = info.chunks.size();

				static constexpr size_t bits_per_packet {8u*512u};

				if (qe.o.all_of<ObjComp::F::TagLocalHaveAll>()) {
					// send have all
					_neep.send_ft1_have_all(
						group_number, peer_number,
						static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO),
						info_hash.data(), info_hash.size()
					);
				} else if (const auto* lhb = qe.o.try_get<ObjComp::F::LocalHaveBitset>(); lhb != nullptr) {
					for (size_t i = 0; i < total_chunks; i += bits_per_packet) {
						size_t bits_this_packet = std::min<size_t>(bits_per_packet, total_chunks-i);

						BitSet have(bits_this_packet); // default init to zero

						// TODO: optimize selective copy bitset
						for (size_t j = i; j < i+bits_this_packet; j++) {
							if (lhb->have[j]) {
								have.set(j-i);
							}
						}

						// TODO: this bursts, dont
						_neep.send_ft1_bitset(
							group_number, peer_number,
							static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO),
							info_hash.data(), info_hash.size(),
							i,
							have._bytes.data(), have.size_bytes()
						);
					}
				} // else, we have nothing *shrug*
			}

			_queue_send_bitset.pop_front();
		}
	}

	// if we have not reached the total cap for transfers
	// count running transfers
	size_t running_sending_transfer_count {_sending_transfers.size()};
	size_t running_receiving_transfer_count {_receiving_transfers.size()};

	if (running_sending_transfer_count < _max_concurrent_out) {
		// TODO: for each peer? transfer cap per peer?
		// TODO: info queue
		if (!_queue_requested_chunk.empty()) { // then check for chunk requests
			const auto [group_number, peer_number, ce, chunk_hash, _] = _queue_requested_chunk.front();

			auto chunk_idx_vec = ce.get<Components::FT1ChunkSHA1Cache>().chunkIndices(chunk_hash);
			if (!chunk_idx_vec.empty()) {

				// check if already sending
				if (!_sending_transfers.containsPeerChunk(group_number, peer_number, ce, chunk_idx_vec.front())) {
					const auto& info = ce.get<Components::FT1InfoSHA1>();

					uint8_t transfer_id {0};
					if (_nft.NGC_FT1_send_init_private(
						group_number, peer_number,
						static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_CHUNK),
						chunk_hash.data.data(), chunk_hash.size(),
						chunkSize(info, chunk_idx_vec.front()),
						&transfer_id
					)) {
						_sending_transfers.emplaceChunk(
							group_number, peer_number,
							transfer_id,
							SendingTransfers::Entry::Chunk{
								ce,
								chunk_idx_vec.front()
							}
						);
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
		}
	}

	// ran regardless of _max_concurrent_in
	// new chunk picker code
	// TODO: need to either split up or remove some things here
	Systems::chunk_picker_updates(
		_cr,
		_os.registry(),
		peer_open_requests,
		_receiving_transfers,
		_nft,
		delta
	);

	// transfer statistics systems
	Systems::transfer_tally_update(_os.registry(), getTimeNow());

	if (peer_open_requests.empty()) {
		return 2.f;
	} else {
		// pretty conservative and should be ajusted on a per peer, per delay basis
		// seems to do the trick
		return 0.05f;
	}
}

// gets called back on main thread after a "new" file info got built on a different thread
void SHA1_NGCFT1::onSendFileHashFinished(ObjectHandle o, Message3Registry* reg_ptr, Contact3 c, uint64_t ts) {
	// sanity
	if (!o.all_of<Components::FT1InfoSHA1, Components::FT1InfoSHA1Hash>()) {
		assert(false);
		return;
	}

	// update content lookup
	const auto& info_hash = o.get<Components::FT1InfoSHA1Hash>().hash;
	_info_to_content[info_hash] = o;

	// update chunk lookup
	const auto& cc = o.get<Components::FT1ChunkSHA1Cache>();
	const auto& info = o.get<Components::FT1InfoSHA1>();
	for (size_t i = 0; i < info.chunks.size(); i++) {
		_chunks[info.chunks[i]] = o;
	}

	// remove from info request queue
	if (auto it = std::find(_queue_content_want_info.begin(), _queue_content_want_info.end(), o); it != _queue_content_want_info.end()) {
		_queue_content_want_info.erase(it);
	}

	// TODO: we dont want chunks anymore
	// TODO: make sure to abort every receiving transfer (sending info and chunk should be fine, info uses copy and chunk handle)

	// something happend, update all chunk pickers
	if (o.all_of<Components::SuspectedParticipants>()) {
		for (const auto& pcv : o.get<Components::SuspectedParticipants>().participants) {
			Contact3Handle pch{_cr, pcv};
			assert(static_cast<bool>(pch));
			pch.emplace_or_replace<ChunkPickerUpdateTag>();
		}
	}

	// in both cases, private and public, c (contact to) is the target
	o.get_or_emplace<Components::AnnounceTargets>().targets.emplace(c);

	// create message
	const auto c_self = _cr.get<Contact::Components::Self>(c).self;
	if (!_cr.valid(c_self)) {
		std::cerr << "SHA1_NGCFT1 error: failed to get self!\n";
		return;
	}

	const auto msg_e = reg_ptr->create();
	reg_ptr->emplace<Message::Components::ContactTo>(msg_e, c);
	reg_ptr->emplace<Message::Components::ContactFrom>(msg_e, c_self);
	reg_ptr->emplace<Message::Components::Timestamp>(msg_e, ts); // reactive?
	reg_ptr->emplace<Message::Components::Read>(msg_e, ts);

	reg_ptr->emplace<Message::Components::MessageFileObject>(msg_e, o);

	//reg_ptr->emplace<Message::Components::Transfer::TagSending>(msg_e);

	o.get_or_emplace<Components::Messages>().messages.push_back({*reg_ptr, msg_e});

	//reg_ptr->emplace<Message::Components::Transfer::FileKind>(e, file_kind);
	// file id would be sha1_info hash or something
	//reg_ptr->emplace<Message::Components::Transfer::FileID>(e, file_id);

	if (_cr.any_of<Contact::Components::ToxGroupEphemeral>(c)) {
		const uint32_t group_number = _cr.get<Contact::Components::ToxGroupEphemeral>(c).group_number;
		uint32_t message_id = 0;

		// TODO: check return
		_nft.NGC_FT1_send_message_public(group_number, message_id, static_cast<uint32_t>(NGCFT1_file_kind::HASH_SHA1_INFO), info_hash.data(), info_hash.size());
		reg_ptr->emplace<Message::Components::ToxGroupMessageID>(msg_e, message_id);
	} else if (
		// non online group
		_cr.any_of<Contact::Components::ToxGroupPersistent>(c)
	) {
		// create msg_id
		const uint32_t message_id = randombytes_random();
		reg_ptr->emplace<Message::Components::ToxGroupMessageID>(msg_e, message_id);
	} // TODO: else private message

	reg_ptr->get_or_emplace<Message::Components::SyncedBy>(msg_e).ts.try_emplace(c_self, ts);
	reg_ptr->get_or_emplace<Message::Components::ReceivedBy>(msg_e).ts.try_emplace(c_self, ts);

	_rmm.throwEventConstruct(*reg_ptr, msg_e);

	// TODO: place in iterate?
	updateMessages(o); // nop // TODO: remove
}

bool SHA1_NGCFT1::onEvent(const ObjectStore::Events::ObjectUpdate& e) {
	if (!e.e.all_of<ObjComp::Ephemeral::File::ActionTransferAccept>()) {
		return false;
	}

	if (!e.e.all_of<Components::FT1InfoSHA1>()) {
		// not ready to load yet, skip
		return false;
	}
	assert(!e.e.all_of<ObjComp::F::TagLocalHaveAll>());
	assert(!e.e.all_of<Components::FT1ChunkSHA1Cache>());
	assert(!e.e.all_of<Components::FT1File2>());
	//accept(e.e, e.e.get<Message::Components::Transfer::ActionAccept>().save_to_path);

	// first, open file for write(+readback)
	std::string full_file_path{e.e.get<ObjComp::Ephemeral::File::ActionTransferAccept>().save_to_path};
	// TODO: replace with filesystem or something
	// TODO: use bool in action !!!
	if (full_file_path.back() != '/') {
		full_file_path += "/";
	}

	// ensure dir exists
	std::filesystem::create_directories(full_file_path);

	const auto& info = e.e.get<Components::FT1InfoSHA1>();
	full_file_path += info.file_name;

	e.e.emplace<ObjComp::F::SingleInfoLocal>(full_file_path);

	const bool file_exists = std::filesystem::exists(full_file_path);
	std::unique_ptr<File2I> file_impl = construct_file2_rw_mapped(full_file_path, info.file_size);

	if (!file_impl->isGood()) {
		std::cerr << "SHA1_NGCFT1 error: failed opening file '" << full_file_path << "'!\n";
		// we failed opening that filepath, so we should offer the user the oportunity to save it differently
		e.e.remove<ObjComp::Ephemeral::File::ActionTransferAccept>(); // stop
		return false;
	}

	{ // next, create chuck cache and check for existing data
		auto& transfer_stats = e.e.get_or_emplace<ObjComp::Ephemeral::File::TransferStats>();
		auto& lhb = e.e.get_or_emplace<ObjComp::F::LocalHaveBitset>();
		if (lhb.have.size_bits() < info.chunks.size()) {
			lhb.have = BitSet{info.chunks.size()};
		}
		auto& cc = e.e.emplace<Components::FT1ChunkSHA1Cache>();
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
						lhb.have.set(i);
						cc.have_count += 1;

						// TODO: replace with some progress counter?
						// or move have_count/want_count or something?
						transfer_stats.total_down += chunk_size;
						//std::cout << "existing i[" << info.chunks.at(i) << "] == d[" << data_hash << "]\n";
					} else {
						//std::cout << "unk i[" << info.chunks.at(i) << "] != d[" << data_hash << "]\n";
					}
				} else {
					// error reading?
				}

				_chunks[info.chunks[i]] = e.e;
				cc.chunk_hash_to_index[info.chunks[i]].push_back(i);
			}
			std::cout << "preexisting " << cc.have_count << "/" << info.chunks.size() << "\n";

			if (cc.have_count >= info.chunks.size()) {
				e.e.emplace_or_replace<ObjComp::F::TagLocalHaveAll>();
				e.e.remove<ObjComp::F::LocalHaveBitset>();
			}
		} else {
			for (size_t i = 0; i < info.chunks.size(); i++) {
				_chunks[info.chunks[i]] = e.e;
				cc.chunk_hash_to_index[info.chunks[i]].push_back(i);
			}
		}
	}

	e.e.emplace_or_replace<Components::FT1File2>(std::move(file_impl));

	// queue announce that we are participating
	e.e.get_or_emplace<Components::ReAnnounceTimer>(0.1f, 60.f*(_rng()%5120) / 1024.f).timer = (_rng()%512) / 1024.f;

	e.e.remove<ObjComp::Ephemeral::File::TagTransferPaused>();

	// start requesting from all participants
	if (e.e.all_of<Components::SuspectedParticipants>()) {
		std::cout << "accepted ft has " << e.e.get<Components::SuspectedParticipants>().participants.size() << " sp\n";
		for (const auto cv : e.e.get<Components::SuspectedParticipants>().participants) {
			_cr.emplace_or_replace<ChunkPickerUpdateTag>(cv);
		}
	} else {
		std::cout << "accepted ft has NO sp!\n";
	}

	e.e.remove<ObjComp::Ephemeral::File::ActionTransferAccept>();

	updateMessages(e.e);

	return false; // ?
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

		auto o = _info_to_content.at(info_hash);

		if (!o.all_of<Components::FT1InfoSHA1Data>()) {
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
			o.get<Components::FT1InfoSHA1Data>().data.size(),
			&transfer_id
		);

		_sending_transfers.emplaceInfo(
			e.group_number, e.peer_number,
			transfer_id,
			SendingTransfers::Entry::Info{
				o.get<Components::FT1InfoSHA1Data>().data
			}
		);

		const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
		_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround
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
			_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround
			if (addParticipation(c, o)) {
				// something happend, update chunk picker
				assert(static_cast<bool>(c));
				c.emplace_or_replace<ChunkPickerUpdateTag>();
			}
		}

		assert(o.all_of<Components::FT1ChunkSHA1Cache>());

		if (!o.get<Components::FT1ChunkSHA1Cache>().haveChunk(o, chunk_hash)) {
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

		const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
		_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround
	} else if (e.file_kind == NGCFT1_file_kind::HASH_SHA1_CHUNK) {
		SHA1Digest sha1_chunk_hash {e.file_id, e.file_id_size};

		if (!_chunks.count(sha1_chunk_hash)) {
			// no idea about this content
			return false;
		}

		auto o = _chunks.at(sha1_chunk_hash);

		{ // they have the content (probably, might be fake, should move this to done)
			const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
			_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround
			if (addParticipation(c, o)) {
				// something happend, update chunk picker
				assert(static_cast<bool>(c));
				c.emplace_or_replace<ChunkPickerUpdateTag>();
			}
		}

		assert(o.all_of<Components::FT1InfoSHA1>());
		assert(o.all_of<Components::FT1ChunkSHA1Cache>());

		const auto& cc = o.get<Components::FT1ChunkSHA1Cache>();
		if (cc.haveChunk(o, sha1_chunk_hash)) {
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

		// now running, remove from requested
		for (const auto it : _receiving_transfers.getTransfer(e.group_number, e.peer_number, e.transfer_id).getChunk().chunk_indices) {
			o.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks.erase(it);
		}

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

		const auto chunk_size = o.get<Components::FT1InfoSHA1>().chunk_size;
		for (const auto chunk_index : transfer.getChunk().chunk_indices) {
			const auto offset_into_file = chunk_index * chunk_size;

			auto* file2 = objGetFile2Write(o);
			if (file2 == nullptr) {
				return false; // early out
			}
			if (!file2->write({e.data, e.data_size}, offset_into_file + e.data_offset)) {
				std::cerr << "SHA1_NGCFT1 error: writing file failed o:" << entt::to_integral(o.entity()) << "@" << offset_into_file + e.data_offset << "\n";
			}
		}

		auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
		if (static_cast<bool>(c)) {
			o.get_or_emplace<Components::TransferStatsTally>()
				.tally[c]
				.recently_received
				.push_back(
					Components::TransferStatsTally::Peer::Entry{
						float(getTimeNow()),
						e.data_size
					}
				)
			;
		}
	} else {
		assert(false && "unhandled case");
	}

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_send_data& e) {
	if (!_sending_transfers.containsPeerTransfer(e.group_number, e.peer_number, e.transfer_id)) {
		return false;
	}

	auto& transfer = _sending_transfers.getTransfer(e.group_number, e.peer_number, e.transfer_id);
	transfer.time_since_activity = 0.f;

	if (transfer.isInfo()) {
		auto& info_transfer = transfer.getInfo();
		for (size_t i = 0; i < e.data_size && (i + e.data_offset) < info_transfer.info_data.size(); i++) {
			e.data[i] = info_transfer.info_data[i + e.data_offset];
		}
	} else if (transfer.isChunk()) {
		auto& chunk_transfer = transfer.getChunk();
		const auto& info = chunk_transfer.content.get<Components::FT1InfoSHA1>();

		auto* file2 = objGetFile2Read(chunk_transfer.content);
		if (file2 == nullptr) {
			// return true?
			return false; // early out
		}

		const auto data = file2->read(
			e.data_size,
			(chunk_transfer.chunk_index * uint64_t(info.chunk_size)) + e.data_offset
		);

		// TODO: optimize
		for (size_t i = 0; i < e.data_size && i < data.size; i++) {
			e.data[i] = data[i];
		}

		// TODO: add event to propergate to messages
		//_rmm.throwEventUpdate(transfer); // should we?

		auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
		if (static_cast<bool>(c)) {
			chunk_transfer.content.get_or_emplace<Components::TransferStatsTally>()
				.tally[c]
				.recently_sent
				.push_back(
					Components::TransferStatsTally::Peer::Entry{
						float(getTimeNow()),
						data.size
					}
				)
			;
		}
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
			auto& file_info = o.emplace_or_replace<ObjComp::F::SingleInfo>(ft_info.file_name, ft_info.file_size);
			//auto& file_info = o.emplace_or_replace<Message::Components::Transfer::FileInfo>();
			//file_info.file_list.emplace_back() = {ft_info.file_name, ft_info.file_size};
			//file_info.total_size = ft_info.file_size;
		}

		std::cout << "SHA1_NGCFT1: got info for [" << SHA1Digest{hash} << "]\n" << ft_info << "\n";

		o.remove<Components::ReRequestInfoTimer>();
		if (auto it = std::find(_queue_content_want_info.begin(), _queue_content_want_info.end(), o); it != _queue_content_want_info.end()) {
			_queue_content_want_info.erase(it);
		}

		o.emplace_or_replace<ObjComp::Ephemeral::File::TagTransferPaused>();

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

		auto* file2 = objGetFile2Read(o);
		if (file2 == nullptr) {
			// rip
			return false;
		}
		auto chunk_data = std::move(file2->read(chunk_size, offset_into_file));
		assert(!chunk_data.empty());

		// check hash of chunk
		auto got_hash = hash_sha1(chunk_data.ptr, chunk_data.size);
		if (info.chunks.at(chunk_index) == got_hash) {
			std::cout << "SHA1_NGCFT1: got chunk [" << SHA1Digest{got_hash} << "]\n";

			if (!o.all_of<ObjComp::F::TagLocalHaveAll>()) {
				{
					auto& lhb = o.get_or_emplace<ObjComp::F::LocalHaveBitset>(BitSet{info.chunks.size()});
					for (const auto inner_chunk_index : transfer.getChunk().chunk_indices) {
						if (lhb.have[inner_chunk_index]) {
							continue;
						}

						// new good chunk

						lhb.have.set(inner_chunk_index);
						cc.have_count += 1;

						// TODO: have wasted + metadata
						//o.get_or_emplace<Message::Components::Transfer::BytesReceived>().total += chunk_data.size;
						// we already tallied all of them but maybe we want to set some other progress indicator here?

						if (cc.have_count == info.chunks.size()) {
							// debug check
							for ([[maybe_unused]] size_t i = 0; i < info.chunks.size(); i++) {
								assert(lhb.have[i]);
							}

							o.emplace_or_replace<ObjComp::F::TagLocalHaveAll>();
							std::cout << "SHA1_NGCFT1: got all chunks for \n" << info << "\n";

							// HACK: close file2, to clear ram
							// TODO: just add a lastActivity comp and close files every x minutes based on that
							file2 = nullptr; // making sure we dont have a stale ptr
							o.remove<Components::FT1File2>(); // will be recreated on demand
							break;
						}
					}
				}
				if (o.all_of<ObjComp::F::TagLocalHaveAll>()) {
					o.remove<ObjComp::F::LocalHaveBitset>(); // save space
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
			} else {
				std::cout << "SHA1_NGCFT1 warning: got chunk duplicate\n";
			}

			// something happend, update chunk picker
			auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
			assert(static_cast<bool>(c));
			c.emplace_or_replace<ChunkPickerUpdateTag>();
		} else {
			// bad chunk
			std::cout << "SHA1_NGCFT1: got BAD chunk from " << e.group_number << ":" << e.peer_number << " [" << info.chunks.at(chunk_index) << "] ; instead got [" << SHA1Digest{got_hash} << "]\n";
		}

		// remove from requested
		// TODO: remove at init and track running transfers differently
		// should be done, double check later
		for (const auto it : transfer.getChunk().chunk_indices) {
			o.get_or_emplace<Components::FT1ChunkSHA1Requested>().chunks.erase(it);
		}

		updateMessages(o); // mostly for received bytes
	}

	_receiving_transfers.removePeerTransfer(e.group_number, e.peer_number, e.transfer_id);

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_send_done& e) {
	if (!_sending_transfers.containsPeerTransfer(e.group_number, e.peer_number, e.transfer_id)) {
		return false;
	}

	auto& transfer = _sending_transfers.getTransfer(e.group_number, e.peer_number, e.transfer_id);

	if (transfer.isChunk()) {
		updateMessages(transfer.getChunk().content); // mostly for sent bytes
	}

	_sending_transfers.removePeerTransfer(e.group_number, e.peer_number, e.transfer_id);

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCFT1_recv_message& e) {
	if (e.file_kind != NGCFT1_file_kind::HASH_SHA1_INFO) {
		return false;
	}

	uint64_t ts = Message::getTimeMS();

	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround
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

	//reg.emplace<Message::Components::Transfer::TagReceiving>(new_msg_e); // add sending?

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
	ObjectHandle o;
	if (_info_to_content.count(sha1_info_hash)) {
		o = _info_to_content.at(sha1_info_hash);
		std::cout << "SHA1_NGCFT1: new message has existing content\n";
	} else {
		// TODO: backend
		o = _mfb.newObject(ByteSpan{sha1_info_hash});
		_info_to_content[sha1_info_hash] = o;
		o.emplace<Components::FT1InfoSHA1Hash>(sha1_info_hash);
		std::cout << "SHA1_NGCFT1: new message has new content\n";
	}
	o.get_or_emplace<Components::Messages>().messages.push_back({reg, new_msg_e});
	reg_ptr->emplace<Message::Components::MessageFileObject>(new_msg_e, o);

	// HACK: assume the message sender is participating. usually a safe bet.
	if (addParticipation(c, o)) {
		// something happend, update chunk picker
		assert(static_cast<bool>(c));
		c.emplace_or_replace<ChunkPickerUpdateTag>();
	}

	// HACK: assume the message sender has all
	o.get_or_emplace<Components::RemoteHaveBitset>().others[c] = {true, {}};

	if (!o.all_of<Components::ReRequestInfoTimer>() && !o.all_of<Components::FT1InfoSHA1>()) {
		// TODO: check if already receiving
		_queue_content_want_info.push_back(o);
	}

	// since public
	o.get_or_emplace<Components::AnnounceTargets>().targets.emplace(c.get<Contact::Components::Parent>().parent);

	// TODO: queue info dl
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
	uint64_t ts = Message::getTimeMS();

	_mfb.newFromFile(
		file_name, file_path,
		[this, reg_ptr, c, ts](ObjectHandle o) { onSendFileHashFinished(o, reg_ptr, c, ts); }
	);

	return true;
}

bool SHA1_NGCFT1::onToxEvent(const Tox_Event_Group_Peer_Join* e) {
	const auto group_number = tox_event_group_peer_join_get_group_number(e);
	const auto peer_number = tox_event_group_peer_join_get_peer_id(e);

	auto c_peer = _tcm.getContactGroupPeer(group_number, peer_number);
	auto c_group = _tcm.getContactGroup(group_number);

	// search for group and/or peer in announce targets
	_os.registry().view<Components::AnnounceTargets, Components::ReAnnounceTimer>().each([this, c_peer, c_group](const auto ov, const Components::AnnounceTargets& at, Components::ReAnnounceTimer& rat) {
		if (at.targets.contains(c_group) || at.targets.contains(c_peer)) {
			rat.lower();
		}
	});

	return false;
}

bool SHA1_NGCFT1::onToxEvent(const Tox_Event_Group_Peer_Exit* e) {
	const auto group_number = tox_event_group_peer_exit_get_group_number(e);
	const auto peer_number = tox_event_group_peer_exit_get_peer_id(e);

	// peer disconnected
	// - remove from all participantions

	{
		// FIXME: this does not work, tcm just delteded the relation ship
		//auto c = _tcm.getContactGroupPeer(group_number, peer_number);

		const auto c_it = _tox_peer_to_contact.find(combine_ids(group_number, peer_number));
		if (c_it == _tox_peer_to_contact.end()) {
			return false;
		}
		auto c = c_it->second;
		if (!static_cast<bool>(c)) {
			return false;
		}

		c.remove<ChunkPicker, ChunkPickerUpdateTag, ChunkPickerTimer>();

		for (const auto& [_, o] : _info_to_content) {
			removeParticipation(c, o);

			if (o.all_of<Components::RemoteHaveBitset>()) {
				o.get<Components::RemoteHaveBitset>().others.erase(c);
			}
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
	std::cerr << "SHA1_NGCFT1: got FT1_HAVE s:" << e.chunks.size() << "\n";

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
	assert(static_cast<bool>(c));
	_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround

	// we might not know yet
	if (addParticipation(c, o)) {
		// something happend, update chunk picker
		c.emplace_or_replace<ChunkPickerUpdateTag>();
	}

	auto& remote_have = o.get_or_emplace<Components::RemoteHaveBitset>().others;
	if (!remote_have.contains(c)) {
		// init
		remote_have.emplace(c, Components::RemoteHaveBitset::Entry{false, num_total_chunks});

		// new have? nice
		// (always update on biset, not always on have)
		c.emplace_or_replace<ChunkPickerUpdateTag>();
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
	std::cerr << "SHA1_NGCFT1: got FT1_BITSET o:" << e.start_chunk << " s:" << e.chunk_bitset.size()*8 << "\n";

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
	// +7 for byte rounding
	if (num_total_chunks+7 < e.start_chunk + (e.chunk_bitset.size()*8)) {
		std::cerr << "SHA1_NGCFT1 error: got bitset.size+start that is larger then number of chunks!!\n";
		std::cerr << "total:" << num_total_chunks << " start:" << e.start_chunk << " size:" << e.chunk_bitset.size()*8 << "\n";
		return false;
	}

	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	assert(static_cast<bool>(c));
	_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround

	// we might not know yet
	addParticipation(c, o);

	auto& remote_have = o.get_or_emplace<Components::RemoteHaveBitset>().others;
	if (!remote_have.contains(c)) {
		// init
		remote_have.emplace(c, Components::RemoteHaveBitset::Entry{false, num_total_chunks});
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

	// new have? nice
	// (always update on bitset, not always on have)
	c.emplace_or_replace<ChunkPickerUpdateTag>();

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCEXT_ft1_have_all& e) {
	std::cerr << "SHA1_NGCFT1: got FT1_HAVE_ALL s:" << e.file_id.size() << "\n";

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

	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	assert(static_cast<bool>(c));
	_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround

	// we might not know yet
	addParticipation(c, o);

	auto& remote_have = o.get_or_emplace<Components::RemoteHaveBitset>().others;
	remote_have[c] = Components::RemoteHaveBitset::Entry{true, {}};

	// new have? nice
	// (always update on have_all, not always on have)
	c.emplace_or_replace<ChunkPickerUpdateTag>();

	return true;
}

bool SHA1_NGCFT1::onEvent(const Events::NGCEXT_pc1_announce& e) {
	std::cerr << "SHA1_NGCFT1: got PC1_ANNOUNCE s:" << e.id.size() << "\n";
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

	// add to participants
	const auto c = _tcm.getContactGroupPeer(e.group_number, e.peer_number);
	_tox_peer_to_contact[combine_ids(e.group_number, e.peer_number)] = c; // workaround
	auto o = itc_it->second;
	if (addParticipation(c, o)) {
		// something happend, update chunk picker
		// !!! this is probably too much
		assert(static_cast<bool>(c));
		c.emplace_or_replace<ChunkPickerUpdateTag>();

		std::cout << "SHA1_NGCFT1: and we where interested!\n";
		// we should probably send the bitset back here / add to queue (can be multiple packets)
		if (o.all_of<Components::FT1ChunkSHA1Cache>() && o.get<Components::FT1ChunkSHA1Cache>().have_count > 0) {
			queueBitsetSendFull(c, o);
		}
	}

	return false;
}

