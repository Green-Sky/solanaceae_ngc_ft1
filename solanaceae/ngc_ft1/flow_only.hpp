#pragma once

#include "./cca.hpp"

#include <chrono>
#include <vector>
#include <tuple>

struct FlowOnly : public CCAI {
	using clock = std::chrono::steady_clock;

	public: // config
		static constexpr float RTT_EMA_ALPHA = 0.1f; // might need over time
		static constexpr float RTT_MAX = 2.f; // 2 sec is probably too much

		//float max_byterate_allowed {1.f*1024*1024}; // 1MiB/s
		//float max_byterate_allowed {0.6f*1024*1024}; // 600MiB/s
		float max_byterate_allowed {0.5f*1024*1024}; // 500MiB/s
		//float max_byterate_allowed {0.05f*1024*1024}; // 50KiB/s
		//float max_byterate_allowed {0.15f*1024*1024}; // 150KiB/s

	private:
		// initialize to low value, will get corrected very fast
		float _fwnd {0.01f * max_byterate_allowed}; // in bytes

		// rtt exponental moving average
		float _rtt_ema {0.1f};

		// list of sequence ids and timestamps of when they where sent (and payload size)
		std::vector<std::tuple<SeqIDType, float, size_t>> _in_flight;
		int64_t _in_flight_bytes {0};

		clock::time_point _time_start_offset;

	private:
		// make values relative to algo start for readability (and precision)
		// get timestamp in seconds
		double getTimeNow(void) const {
			return std::chrono::duration<double>{clock::now() - _time_start_offset}.count();
		}

		// moving avg over the last few delay samples
		// VERY sensitive to bundling acks
		float getCurrentDelay(void) const;

		void addRTT(float new_delay);

		void updateWindow(void);

	public: // api
		FlowOnly(size_t maximum_segment_data_size) : CCAI(maximum_segment_data_size) {}

		// TODO: api for how much data we should send
		// take time since last sent into account
		// respect max_byterate_allowed
		size_t canSend(void) override;

		// get the list of timed out seq_ids
		std::vector<SeqIDType> getTimeouts(void) const override;

	public: // callbacks
		// data size is without overhead
		void onSent(SeqIDType seq, size_t data_size) override;

		void onAck(std::vector<SeqIDType> seqs) override;

		// if discard, not resent, not inflight
		void onLoss(SeqIDType seq, bool discard) override;
};
