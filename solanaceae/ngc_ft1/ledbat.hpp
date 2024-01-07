#pragma once

#include "./cca.hpp"

#include <chrono>
#include <deque>
#include <vector>
#include <cstdint>

// LEDBAT: https://www.rfc-editor.org/rfc/rfc6817
// LEDBAT++: https://www.ietf.org/archive/id/draft-irtf-iccrg-ledbat-plus-plus-01.txt

// LEDBAT++ implementation
struct LEDBAT : public CCAI {
	public: // config
#if 0
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

		//static constexpr size_t maximum_segment_size {496 + segment_overhead}; // tox 500 - 4 from ft
		const size_t MAXIMUM_SEGMENT_SIZE {MAXIMUM_SEGMENT_DATA_SIZE + SEGMENT_OVERHEAD}; // tox 500 - 4 from ft
		//static_assert(maximum_segment_size == 574); // mesured in wireshark
#endif

		// ledbat++ says 60ms, we might need other values if relayed
		//const float target_delay {0.060f};
		const float target_delay {0.030f};
		//const float target_delay {0.120f}; // 2x if relayed?

		// TODO: use a factor for multiple of rtt
		static constexpr size_t current_delay_filter_window {16*4};

		//static constexpr size_t rtt_buffer_size_max {2000};

	public:
		LEDBAT(size_t maximum_segment_data_size);

		// return the current believed window in bytes of how much data can be inflight,
		// without overstepping the delay requirement
		float getWindow(void) override {
			return _cwnd;
		}

		// TODO: api for how much data we should send
		// take time since last sent into account
		// respect max_byterate_allowed
		int64_t canSend(float time_delta) override;

		// get the list of timed out seq_ids
		std::vector<SeqIDType> getTimeouts(void) const override;

	public: // callbacks
		// data size is without overhead
		void onSent(SeqIDType seq, size_t data_size) override;

		void onAck(std::vector<SeqIDType> seqs) override;

		// if discard, not resent, not inflight
		void onLoss(SeqIDType seq, bool discard) override;

	private:
		using clock = std::chrono::steady_clock;

		// make values relative to algo start for readability (and precision)
		// get timestamp in seconds
		float getTimeNow(void) const {
			return std::chrono::duration<float>{clock::now() - _time_start_offset}.count();
		}

		// moving avg over the last few delay samples
		// VERY sensitive to bundling acks
		float getCurrentDelay(void) const override;

		void addRTT(float new_delay);

		void updateWindows(void);

	private: // state
		//float _cto {2.f}; // congestion timeout value in seconds

		float _cwnd {2.f * MAXIMUM_SEGMENT_SIZE}; // in bytes
		float _base_delay {2.f}; // lowest mesured delay in _rtt_buffer in seconds

		float _last_cwnd {0.f}; // timepoint of last cwnd correction
		int64_t _recently_acked_data {0}; // reset on _last_cwnd
		int64_t _recently_sent_bytes {0};

		bool _recently_lost_data {false};
		float _last_congestion_event {0.f};
		float _last_congestion_rtt {0.5f};

		// initialize to low value, will get corrected very fast
		float _fwnd {0.01f * max_byterate_allowed}; // in bytes


		// ssthresh

		// spec recomends 10min
		// TODO: optimize and devide into spans of 1min (spec recom)
		std::deque<float> _tmp_rtt_buffer;
		std::deque<std::pair<float, float>> _rtt_buffer; // timepoint, delay
		std::deque<float> _rtt_buffer_minutes;

		// list of sequence ids and timestamps of when they where sent
		std::deque<std::tuple<SeqIDType, float, size_t>> _in_flight;

		int64_t _in_flight_bytes {0};

		SeqIDType _last_ack_got {0xff, 0xffff}; // some default

	private: // helper
		clock::time_point _time_start_offset;
};

