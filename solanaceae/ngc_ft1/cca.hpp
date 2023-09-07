#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

// TODO: refactor, more state tracking in ccai and seperate into flow and congestion algos
inline bool isSkipSeqID(const std::pair<uint8_t, uint16_t>& a, const std::pair<uint8_t, uint16_t>& b) {
	// this is not perfect, would need more ft id based history
	if (a.first != b.first) {
		return false; // we dont know
	} else {
		return a.second+1 != b.second;
	}
}

struct CCAI {
	public: // config
		using SeqIDType = std::pair<uint8_t, uint16_t>; // tf_id, seq_id

		static constexpr size_t IPV4_HEADER_SIZE {20};
		static constexpr size_t IPV6_HEADER_SIZE {40}; // bru
		static constexpr size_t UDP_HEADER_SIZE {8};

		// TODO: tcp AND IPv6 will be different
		static constexpr size_t SEGMENT_OVERHEAD {
			4+ // ft overhead
			46+ // tox?
			UDP_HEADER_SIZE+
			IPV4_HEADER_SIZE
		};

		// TODO: make configurable, set with tox ngc lossy packet size
		//const size_t MAXIMUM_SEGMENT_DATA_SIZE {1000-4};
		const size_t MAXIMUM_SEGMENT_DATA_SIZE {500-4};

		const size_t MAXIMUM_SEGMENT_SIZE {MAXIMUM_SEGMENT_DATA_SIZE + SEGMENT_OVERHEAD}; // tox 500 - 4 from ft
		//static_assert(maximum_segment_size == 574); // mesured in wireshark

		// flow control
		float max_byterate_allowed {10*1024*1024}; // 10MiB/s

	public: // api
		CCAI(size_t maximum_segment_data_size) : MAXIMUM_SEGMENT_DATA_SIZE(maximum_segment_data_size) {}

		// return the current believed window in bytes of how much data can be inflight,
		//virtual float getCWnD(void) const = 0;

		// returns current rtt/delay
		virtual float getCurrentDelay(void) const = 0;

		virtual float getWindow(void) = 0;

		// TODO: api for how much data we should send
		// take time since last sent into account
		// respect max_byterate_allowed
		virtual int64_t canSend(void) = 0;

		// get the list of timed out seq_ids
		virtual std::vector<SeqIDType> getTimeouts(void) const = 0;

	public: // callbacks
		// data size is without overhead
		virtual void onSent(SeqIDType seq, size_t data_size) = 0;

		// TODO: copy???
		virtual void onAck(std::vector<SeqIDType> seqs) = 0;

		// if discard, not resent, not inflight
		virtual void onLoss(SeqIDType seq, bool discard) = 0;
};

