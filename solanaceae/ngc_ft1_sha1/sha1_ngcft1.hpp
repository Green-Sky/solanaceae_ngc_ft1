#pragma once

// solanaceae port of sha1 fts for NGCFT1

#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>
#include <solanaceae/util/bitset.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include "./ft1_sha1_info.hpp"
#include "./receiving_transfers.hpp"

#include <entt/entity/registry.hpp>
#include <entt/entity/handle.hpp>
#include <entt/container/dense_map.hpp>

#include <variant>
#include <random>
#include <atomic>
#include <mutex>
#include <list>

class SHA1_NGCFT1 : public ToxEventI, public RegistryMessageModelEventI, public NGCFT1EventI, public NGCEXTEventI {
	ObjectStore2& _os;
	// TODO: backend abstraction
	Contact3Registry& _cr;
	RegistryMessageModel& _rmm;
	NGCFT1& _nft;
	ToxContactModel2& _tcm;
	ToxEventProviderI& _tep;
	NGCEXTEventProvider& _neep;

	std::minstd_rand _rng {1337*11};

	// limit this to each group?
	entt::dense_map<SHA1Digest, ObjectHandle> _info_to_content;

	// sha1 chunk index
	// TODO: optimize lookup
	// TODO: multiple contents. hashes might be unique, but data is not
	entt::dense_map<SHA1Digest, ObjectHandle> _chunks;

	// group_number, peer_number, content, chunk_hash, timer
	std::deque<std::tuple<uint32_t, uint32_t, ObjectHandle, SHA1Digest, float>> _queue_requested_chunk;
	//void queueUpRequestInfo(uint32_t group_number, uint32_t peer_number, const SHA1Digest& hash);
	void queueUpRequestChunk(uint32_t group_number, uint32_t peer_number, ObjectHandle content, const SHA1Digest& hash);

	struct SendingTransfer {
		struct Info {
			// copy of info data
			// too large?
			std::vector<uint8_t> info_data;
		};

		struct Chunk {
			ObjectHandle content;
			size_t chunk_index; // <.< remove offset_into_file
			//uint64_t offset_into_file;
			// or data?
			// if memmapped, this would be just a pointer
		};

		std::variant<Info, Chunk> v;

		float time_since_activity {0.f};
	};
	// key is groupid + peerid
	entt::dense_map<uint64_t, entt::dense_map<uint8_t, SendingTransfer>> _sending_transfers;

	ReceivingTransfers _receiving_transfers;

	// makes request rotate around open content
	std::deque<ObjectHandle> _queue_content_want_info;

	struct QBitsetEntry {
		Contact3Handle c;
		ObjectHandle o;
	};
	std::deque<QBitsetEntry> _queue_send_bitset;

	// workaround missing contact events
	// only used to remove participation on peer exit
	entt::dense_map<uint64_t, Contact3Handle> _tox_peer_to_contact;

	std::atomic_bool _info_builder_dirty {false};
	std::mutex _info_builder_queue_mutex;
	using InfoBuilderEntry = std::function<void(void)>;
	std::list<InfoBuilderEntry> _info_builder_queue;

	void updateMessages(ObjectHandle ce);

	std::optional<std::pair<uint32_t, uint32_t>> selectPeerForRequest(ObjectHandle ce);

	void queueBitsetSendFull(Contact3Handle c, ObjectHandle o);

	public: // TODO: config
		bool _udp_only {false};

		size_t _max_concurrent_in {4}; // info only
		size_t _max_concurrent_out {4*10}; // HACK: allow "ideal" number for 10 peers

	public:
		SHA1_NGCFT1(
			ObjectStore2& os,
			Contact3Registry& cr,
			RegistryMessageModel& rmm,
			NGCFT1& nft,
			ToxContactModel2& tcm,
			ToxEventProviderI& tep,
			NGCEXTEventProvider& neep
		);

		float iterate(float delta);

	protected: // rmm events (actions)
		bool onEvent(const Message::Events::MessageUpdated&) override;

	protected: // events
		bool onEvent(const Events::NGCFT1_recv_request&) override;
		bool onEvent(const Events::NGCFT1_recv_init&) override;
		bool onEvent(const Events::NGCFT1_recv_data&) override;
		bool onEvent(const Events::NGCFT1_send_data&) override; // const?
		bool onEvent(const Events::NGCFT1_recv_done&) override;
		bool onEvent(const Events::NGCFT1_send_done&) override;
		bool onEvent(const Events::NGCFT1_recv_message&) override;

		bool sendFilePath(const Contact3 c, std::string_view file_name, std::string_view file_path) override;

		bool onToxEvent(const Tox_Event_Group_Peer_Exit* e) override;

		bool onEvent(const Events::NGCEXT_ft1_have&) override;
		bool onEvent(const Events::NGCEXT_ft1_bitset&) override;
		bool onEvent(const Events::NGCEXT_ft1_have_all&) override;

		bool onEvent(const Events::NGCEXT_pc1_announce&) override;
};

