#pragma once

// solanaceae port of tox_ngc_ft1

#include <cstdint>
#include <solanaceae/toxcore/tox_interface.hpp>
#include <solanaceae/toxcore/tox_event_interface.hpp>

#include <solanaceae/ngc_ext/ngcext.hpp>
#include "./cubic.hpp"
//#include "./flow_only.hpp"
//#include "./ledbat.hpp"

#include "./rcv_buf.hpp"
#include "./snd_buf.hpp"

#include "./ngcft1_file_kind.hpp"

#include <map>
#include <set>
#include <memory>

namespace Events {

	struct NGCFT1_recv_request {
		uint32_t group_number;
		uint32_t peer_number;

		NGCFT1_file_kind file_kind;

		const uint8_t* file_id;
		size_t file_id_size;
	};

	struct NGCFT1_recv_init {
		uint32_t group_number;
		uint32_t peer_number;

		NGCFT1_file_kind file_kind;

		const uint8_t* file_id;
		size_t file_id_size;

		const uint8_t transfer_id;
		const size_t file_size;

		// return true to accept, false to deny
		bool& accept;
	};

	struct NGCFT1_recv_data {
		uint32_t group_number;
		uint32_t peer_number;

		uint8_t transfer_id;

		size_t data_offset;
		const uint8_t* data;
		size_t data_size;
	};

	// request to fill data_size bytes into data
	struct NGCFT1_send_data {
		uint32_t group_number;
		uint32_t peer_number;

		uint8_t transfer_id;

		size_t data_offset;
		uint8_t* data;
		size_t data_size;
	};

	struct NGCFT1_recv_done {
		uint32_t group_number;
		uint32_t peer_number;

		uint8_t transfer_id;
		// TODO: reason
	};

	struct NGCFT1_send_done {
		uint32_t group_number;
		uint32_t peer_number;

		uint8_t transfer_id;
		// TODO: reason
	};

	struct NGCFT1_recv_message {
		uint32_t group_number;
		uint32_t peer_number;

		uint32_t message_id;

		NGCFT1_file_kind file_kind;

		const uint8_t* file_id;
		size_t file_id_size;
	};

} // Events

enum class NGCFT1_Event : uint8_t {
	recv_request,
	recv_init,

	recv_data,
	send_data,

	recv_done,
	send_done,

	recv_message,

	MAX
};

struct NGCFT1EventI {
	using enumType = NGCFT1_Event;
	virtual bool onEvent(const Events::NGCFT1_recv_request&) { return false; }
	virtual bool onEvent(const Events::NGCFT1_recv_init&) { return false; }
	virtual bool onEvent(const Events::NGCFT1_recv_data&) { return false; }
	virtual bool onEvent(const Events::NGCFT1_send_data&) { return false; } // const?
	virtual bool onEvent(const Events::NGCFT1_recv_done&) { return false; }
	virtual bool onEvent(const Events::NGCFT1_send_done&) { return false; }
	virtual bool onEvent(const Events::NGCFT1_recv_message&) { return false; }
};

using NGCFT1EventProviderI = EventProviderI<NGCFT1EventI>;

class NGCFT1 : public ToxEventI, public NGCEXTEventI, public NGCFT1EventProviderI {
	ToxI& _t;
	ToxEventProviderI& _tep;
	NGCEXTEventProviderI& _neep;

	// TODO: config
	size_t acks_per_packet {3u}; // 3
	float init_retry_timeout_after {5.f}; // 10sec
	float sending_give_up_after {30.f}; // 30sec


	struct Group {
		struct Peer {
			uint32_t max_packet_data_size {500-4};
			//std::unique_ptr<CCAI> cca = std::make_unique<CUBIC>(max_packet_data_size); // TODO: replace with tox_group_max_custom_lossy_packet_length()-4
			std::unique_ptr<CCAI> cca;

			struct RecvTransfer {
				uint32_t file_kind;
				std::vector<uint8_t> file_id;

				enum class State {
					INITED, //init acked, but no data received yet (might be dropped)
					RECV, // receiving data
				} state;

				// float time_since_last_activity ?
				size_t file_size {0};
				size_t file_size_current {0};

				// sequence id based reassembly
				RecvSequenceBuffer rsb;
			};
			std::array<std::optional<RecvTransfer>, 256> recv_transfers;
			size_t next_recv_transfer_idx {0}; // next id will be 0

			struct SendTransfer {
				uint32_t file_kind;
				std::vector<uint8_t> file_id;

				enum class State {
					INIT_SENT, // keep this state until ack or deny or giveup

					SENDING, // we got the ack and are now sending data

					FINISHING, // we sent all data but acks still outstanding????

					// delete
				} state;

				size_t inits_sent {1}; // is sent when creating

				float time_since_activity {0.f};
				size_t file_size {0};
				size_t file_size_current {0};

				// sequence array
				// list of sent but not acked seq_ids
				SendSequenceBuffer ssb;
			};
			std::array<std::optional<SendTransfer>, 256> send_transfers;
			size_t next_send_transfer_idx {0}; // next id will be 0
			size_t next_send_transfer_send_idx {0};
		};
		std::map<uint32_t, Peer> peers;
	};
	std::map<uint32_t, Group> groups;

	protected:
		bool sendPKG_FT1_REQUEST(uint32_t group_number, uint32_t peer_number, uint32_t file_kind, const uint8_t* file_id, size_t file_id_size);
		bool sendPKG_FT1_INIT(uint32_t group_number, uint32_t peer_number, uint32_t file_kind, uint64_t file_size, uint8_t transfer_id, const uint8_t* file_id, size_t file_id_size);
		bool sendPKG_FT1_INIT_ACK(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id);
		bool sendPKG_FT1_DATA(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id, uint16_t sequence_id, const uint8_t* data, size_t data_size);
		bool sendPKG_FT1_DATA_ACK(uint32_t group_number, uint32_t peer_number, uint8_t transfer_id, const uint16_t* seq_ids, size_t seq_ids_size);
		bool sendPKG_FT1_MESSAGE(uint32_t group_number, uint32_t message_id, uint32_t file_kind, const uint8_t* file_id, size_t file_id_size);

		void updateSendTransfer(float time_delta, uint32_t group_number, uint32_t peer_number, Group::Peer& peer, size_t idx, std::set<CCAI::SeqIDType>& timeouts_set, int64_t& can_packet_size);
		void iteratePeer(float time_delta, uint32_t group_number, uint32_t peer_number, Group::Peer& peer);

	public:
		NGCFT1(
			ToxI& t,
			ToxEventProviderI& tep,
			NGCEXTEventProviderI& neep
		);

		void iterate(float delta);

	public: // ft1 api
		// TODO: public variant?
		void NGC_FT1_send_request_private(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size
		);

		// public does not make sense here
		bool NGC_FT1_send_init_private(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size,
			size_t file_size,
			uint8_t* transfer_id
		);

		// sends the message and fills in message_id
		bool NGC_FT1_send_message_public(
			uint32_t group_number,
			uint32_t& message_id,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size
		);

	protected:
		bool onEvent(const Events::NGCEXT_ft1_request&) override;
		bool onEvent(const Events::NGCEXT_ft1_init&) override;
		bool onEvent(const Events::NGCEXT_ft1_init_ack&) override;
		bool onEvent(const Events::NGCEXT_ft1_data&) override;
		bool onEvent(const Events::NGCEXT_ft1_data_ack&) override;
		bool onEvent(const Events::NGCEXT_ft1_message&) override;

	protected:
		bool onToxEvent(const Tox_Event_Group_Peer_Exit* e) override;
		//bool onToxEvent(const Tox_Event_Group_Custom_Packet* e) override;
		//bool onToxEvent(const Tox_Event_Group_Custom_Private_Packet* e) override;
};

