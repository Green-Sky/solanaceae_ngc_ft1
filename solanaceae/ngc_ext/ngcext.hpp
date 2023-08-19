#pragma once

// solanaceae port of tox_ngc_ext

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/util/event_provider.hpp>

#include <solanaceae/toxcore/tox_key.hpp>

#include <array>
#include <vector>

namespace Events {

	// TODO: implement events as non-owning

	struct NGCEXT_hs1_request_last_ids {
		uint32_t group_number;
		uint32_t peer_number;

		// - peer_key bytes (peer key we want to know ids for)
		ToxKey peer_key;

		// - 1 byte (uint8_t count ids, atleast 1)
		uint8_t count_ids;
	};

	struct NGCEXT_hs1_response_last_ids {
		uint32_t group_number;
		uint32_t peer_number;

		// respond to a request with 0 or more message ids, sorted by newest first
		// - peer_key bytes (the msg_ids are from)
		ToxKey peer_key;

		// - 1 byte (uint8_t count ids, can be 0)
		uint8_t count_ids;

		// - array [
		//   - msg_id bytes (the message id)
		// - ]
		std::vector<uint32_t> msg_ids;
	};

	struct NGCEXT_ft1_request {
		uint32_t group_number;
		uint32_t peer_number;

		// request the other side to initiate a FT
		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;
	};

	struct NGCEXT_ft1_init {
		uint32_t group_number;
		uint32_t peer_number;

		// tell the other side you want to start a FT
		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - 8 bytes (data size)
		uint64_t file_size;

		// - 1 byte (temporary_file_tf_id, for this peer only, technically just a prefix to distinguish between simultainious fts)
		uint8_t transfer_id;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;

		// TODO: max supported lossy packet size
	};

	struct NGCEXT_ft1_init_ack {
		uint32_t group_number;
		uint32_t peer_number;

		// - 1 byte (transfer_id)
		uint8_t transfer_id;

		// TODO: max supported lossy packet size
	};

	struct NGCEXT_ft1_data {
		uint32_t group_number;
		uint32_t peer_number;

		// data fragment
		// - 1 byte (temporary_file_tf_id)
		uint8_t transfer_id;

		// - 2 bytes (sequece id)
		uint16_t sequence_id;

		// - X bytes (the data fragment)
		// (size is implicit)
		std::vector<uint8_t> data;
	};

	struct NGCEXT_ft1_data_ack {
		uint32_t group_number;
		uint32_t peer_number;

		// - 1 byte (temporary_file_tf_id)
		uint8_t transfer_id;

		// - array [ (of sequece ids)
		//   - 2 bytes (sequece id)
		// - ]
		std::vector<uint16_t> sequence_ids;
	};

	struct NGCEXT_ft1_message {
		uint32_t group_number;
		uint32_t peer_number;

		// - 4 byte (message_id)
		uint32_t message_id;

		// request the other side to initiate a FT
		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;
	};

} // Events

enum class NGCEXT_Event : uint8_t {
	//TODO: make it possible to go further back
	// request last (few) message_ids for a peer
	// - peer_key bytes (peer key we want to know ids for)
	// - 1 byte (uint8_t count ids, atleast 1)
	HS1_REQUEST_LAST_IDS = 0x80 | 1u,

	// respond to a request with 0 or more message ids, sorted by newest first
	// - peer_key bytes (the msg_ids are from)
	// - 1 byte (uint8_t count ids, can be 0)
	// - array [
	//   - msg_id bytes (the message id)
	// - ]
	HS1_RESPONSE_LAST_IDS,

	// request the other side to initiate a FT
	// - 4 byte (file_kind)
	// - X bytes (file_kind dependent id, differnt sizes)
	FT1_REQUEST = 0x80 | 8u,

	// TODO: request result negative, speed up not found

	// tell the other side you want to start a FT
	// TODO: might use id layer instead. with it, it would look similar to friends_ft
	// - 4 byte (file_kind)
	// - 8 bytes (data size, can be 0 if unknown, BUT files have to be atleast 1 byte)
	// - 1 byte (temporary_file_tf_id, for this peer only, technically just a prefix to distinguish between simultainious fts)
	// - X bytes (file_kind dependent id, differnt sizes)
	FT1_INIT,

	// acknowlage init (like an accept)
	// like tox ft control continue
	// - 1 byte (transfer_id)
	FT1_INIT_ACK,

	// TODO: init deny, speed up non acceptance

	// data fragment
	// - 1 byte (temporary_file_tf_id)
	// - 2 bytes (sequece id)
	// - X bytes (the data fragment)
	// (size is implicit)
	FT1_DATA,

	// acknowlage data fragments
	// TODO: last 3 should be sufficient, 5 should be generous
	// - 1 byte (temporary_file_tf_id)
	// // this is implicit (pkg size)- 1 byte (number of sequence ids to ack, this kind of depends on window size)
	// - array [ (of sequece ids)
	//   - 2 bytes (sequece id)
	// - ]
	FT1_DATA_ACK,

	// send file as message
	// basically the opposite of request
	// contains file_kind and file_id (and timestamp?)
	// - 4 byte (message_id)
	// - 4 byte (file_kind)
	// - X bytes (file_kind dependent id, differnt sizes)
	FT1_MESSAGE,

	MAX
};

struct NGCEXTEventI {
	using enumType = NGCEXT_Event;
	virtual bool onEvent(const Events::NGCEXT_hs1_request_last_ids&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_hs1_response_last_ids&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_request&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_init&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_init_ack&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_data&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_data_ack&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_message&) { return false; }
};

using NGCEXTEventProviderI = EventProviderI<NGCEXTEventI>;

class NGCEXTEventProvider : public ToxEventI, public NGCEXTEventProviderI {
	ToxEventProviderI& _tep;

	public:
		NGCEXTEventProvider(ToxEventProviderI& tep/*, ToxI& t*/);

	protected:
		bool parse_hs1_request_last_ids(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_hs1_response_last_ids(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_request(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_init(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_init_ack(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_data(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_data_ack(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_message(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool handlePacket(
			const uint32_t group_number,
			const uint32_t peer_number,
			const uint8_t* data,
			const size_t data_size,
			const bool _private
		);

	protected:
		bool onToxEvent(const Tox_Event_Group_Custom_Packet* e) override;
		bool onToxEvent(const Tox_Event_Group_Custom_Private_Packet* e) override;
};

