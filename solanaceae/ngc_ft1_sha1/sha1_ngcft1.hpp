#pragma once

// solanaceae port of sha1 fts for NGCFT1

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include "./ft1_sha1_info.hpp"

#include <entt/entity/registry.hpp>
#include <entt/entity/handle.hpp>
#include <entt/container/dense_map.hpp>

#include <variant>
#include <random>

enum class Content : uint32_t {};
using ContentRegistry = entt::basic_registry<Content>;
using ContentHandle = entt::basic_handle<ContentRegistry>;

class SHA1_NGCFT1 : public RegistryMessageModelEventI, public NGCFT1EventI {
	Contact3Registry& _cr;
	RegistryMessageModel& _rmm;
	NGCFT1& _nft;
	ToxContactModel2& _tcm;

	std::minstd_rand _rng {1337*11};

	// registry per group?
	ContentRegistry _contentr;

	// limit this to each group?
	entt::dense_map<SHA1Digest, ContentHandle> _info_to_content;

	// sha1 chunk index
	// TODO: optimize lookup
	// TODO: multiple contents. hashes might be unique, but data is not
	entt::dense_map<SHA1Digest, ContentHandle> _chunks;

	// group_number, peer_number, content, chunk_hash, timer
	std::deque<std::tuple<uint32_t, uint32_t, ContentHandle, SHA1Digest, float>> _queue_requested_chunk;
	//void queueUpRequestInfo(uint32_t group_number, uint32_t peer_number, const SHA1Digest& hash);
	void queueUpRequestChunk(uint32_t group_number, uint32_t peer_number, ContentHandle content, const SHA1Digest& hash);

	struct SendingTransfer {
		struct Info {
			// copy of info data
			// too large?
			std::vector<uint8_t> info_data;
		};

		struct Chunk {
			ContentHandle content;
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

	struct ReceivingTransfer {
		struct Info {
			ContentHandle content;
			// copy of info data
			// too large?
			std::vector<uint8_t> info_data;
		};

		struct Chunk {
			ContentHandle content;
			std::vector<size_t> chunk_indices;
			// or data?
			// if memmapped, this would be just a pointer
		};

		std::variant<Info, Chunk> v;

		float time_since_activity {0.f};
	};
	// key is groupid + peerid
	entt::dense_map<uint64_t, entt::dense_map<uint8_t, ReceivingTransfer>> _receiving_transfers;

	// makes request rotate around open content
	std::deque<ContentHandle> _queue_content_want_info;
	std::deque<ContentHandle> _queue_content_want_chunk;

	static uint64_t combineIds(const uint32_t group_number, const uint32_t peer_number);

	void updateMessages(ContentHandle ce);

	std::optional<std::pair<uint32_t, uint32_t>> selectPeerForRequest(ContentHandle ce);

	public: // TODO: config
		bool _udp_only {false};

		size_t _max_concurrent_in {4};
		size_t _max_concurrent_out {6};
		// TODO: probably also includes running transfers rn (meh)
		size_t _max_pending_requests {16}; // per content

	public:
		SHA1_NGCFT1(
			Contact3Registry& cr,
			RegistryMessageModel& rmm,
			NGCFT1& nft,
			ToxContactModel2& tcm
		);

		void iterate(float delta);

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
};

