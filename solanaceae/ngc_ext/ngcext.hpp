#pragma once

// solanaceae port of tox_ngc_ext

#include <solanaceae/toxcore/tox_event_interface.hpp>
#include <solanaceae/toxcore/tox_interface.hpp>
#include <solanaceae/util/event_provider.hpp>

#include <solanaceae/toxcore/tox_key.hpp>

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
	};

	struct NGCEXT_ft1_init_ack {
		uint32_t group_number;
		uint32_t peer_number;

		// - 1 byte (transfer_id)
		uint8_t transfer_id;

		// - 2 byte (self_max_lossy_data_size)
		uint16_t max_lossy_data_size;
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

		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;
	};

	struct NGCEXT_ft1_have {
		uint32_t group_number;
		uint32_t peer_number;

		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;

		// - array [
		//   - 4 bytes (chunk index)
		// - ]
		std::vector<uint32_t> chunks;
	};

	struct NGCEXT_ft1_bitset {
		uint32_t group_number;
		uint32_t peer_number;

		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;

		uint32_t start_chunk;

		// - array [
		//   - 1 bit (have chunk)
		// - ] (filled up with zero)
		// high to low?
		std::vector<uint8_t> chunk_bitset;
	};

	struct NGCEXT_ft1_have_all {
		uint32_t group_number;
		uint32_t peer_number;

		// - 4 byte (file_kind)
		uint32_t file_kind;

		// - X bytes (file_kind dependent id, differnt sizes)
		std::vector<uint8_t> file_id;
	};

	struct NGCEXT_pc1_announce {
		uint32_t group_number;
		uint32_t peer_number;

		// - X bytes (id, differnt sizes)
		std::vector<uint8_t> id;
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
	// - 2 byte (self_max_lossy_data_size) (optional since v2)
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
	// - 4 bytes (message_id)
	// - 4 bytes (file_kind)
	// - X bytes (file_kind dependent id, differnt sizes)
	FT1_MESSAGE,

	// announce you have specified chunks, for given info
	// this is info/chunk specific
	// bundle these together to reduce overhead (like maybe every 16, max 1min)
	// - 4 bytes (file_kind)
	// - X bytes (file_kind dependent id, differnt sizes)
	// - array [
	//   - 4 bytes (chunk index)
	// - ]
	FT1_HAVE,

	// tell the other peer which chunks, for a given info you have
	// compressed down to a bitset (in parts)
	// supposed to only be sent once on participation announcement, when mutual interest
	// it is always assumed by the other side, that you dont have the chunk, until told otherwise,
	// so you can be smart about what you send.
	// - 4 bytes (file_kind)
	// - X bytes (file_kind dependent id, differnt sizes)
	// - 4 bytes (first chunk index in bitset)
	// - array [
	//   - 1 bit (have chunk)
	// - ] (filled up with zero)
	FT1_BITSET,

	// announce you have all chunks, for given info
	// prefer over have and bitset
	// - 4 bytes (file_kind)
	// - X bytes (file_kind dependent id, differnt sizes)
	FT1_HAVE_ALL,

	// TODO: FT1_IDONTHAVE, tell a peer you no longer have said chunk
	// TODO: FT1_REJECT, tell a peer you wont fulfil the request

	// tell another peer that you are participating in X
	// you can reply with PC1_ANNOUNCE, to let the other side know, you too are participating in X
	// you should NOT announce often, since this hits peers that not participate
	// ft1 uses fk+id
	// - x bytes (id, different sizes)
	PC1_ANNOUNCE = 0x80 | 32u,

	// uses sub splitting
	P2PRNG = 0x80 | 38u,

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
	virtual bool onEvent(const Events::NGCEXT_ft1_have&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_bitset&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_ft1_have_all&) { return false; }
	virtual bool onEvent(const Events::NGCEXT_pc1_announce&) { return false; }
};

using NGCEXTEventProviderI = EventProviderI<NGCEXTEventI>;

class NGCEXTEventProvider : public ToxEventI, public NGCEXTEventProviderI {
	ToxI& _t;
	ToxEventProviderI& _tep;

	public:
		NGCEXTEventProvider(ToxI& t, ToxEventProviderI& tep);

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

		bool parse_ft1_init_ack_v2(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_have(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_bitset(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_ft1_have_all(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* data, size_t data_size,
			bool _private
		);

		bool parse_pc1_announce(
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

	public: // send api
		bool send_ft1_request(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size
		);

		bool send_ft1_init(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			uint64_t file_size,
			uint8_t transfer_id,
			const uint8_t* file_id, size_t file_id_size
		);

		bool send_ft1_init_ack(
			uint32_t group_number, uint32_t peer_number,
			uint8_t transfer_id
		);

		bool send_ft1_data(
			uint32_t group_number, uint32_t peer_number,
			uint8_t transfer_id,
			uint16_t sequence_id,
			const uint8_t* data, size_t data_size
		);

		bool send_ft1_data_ack(
			uint32_t group_number, uint32_t peer_number,
			uint8_t transfer_id,
			const uint16_t* seq_ids, size_t seq_ids_size
		);

		// TODO: add private version
		bool send_all_ft1_message(
			uint32_t group_number,
			uint32_t message_id,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size
		);

		bool send_ft1_have(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size,
			const uint32_t* chunks_data, size_t chunks_size
		);

		bool send_ft1_bitset(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size,
			uint32_t start_chunk,
			const uint8_t* bitset_data, size_t bitset_size // size is bytes
		);

		bool send_ft1_have_all(
			uint32_t group_number, uint32_t peer_number,
			uint32_t file_kind,
			const uint8_t* file_id, size_t file_id_size
		);

		bool send_pc1_announce(
			uint32_t group_number, uint32_t peer_number,
			const uint8_t* id_data, size_t id_size
		);

		bool send_all_pc1_announce(
			uint32_t group_number,
			const uint8_t* id_data, size_t id_size
		);

	protected:
		bool onToxEvent(const Tox_Event_Group_Custom_Packet* e) override;
		bool onToxEvent(const Tox_Event_Group_Custom_Private_Packet* e) override;
};

