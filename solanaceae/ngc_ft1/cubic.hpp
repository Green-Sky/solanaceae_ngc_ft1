#pragma once

#include "./flow_only.hpp"

#include <chrono>

struct CUBIC : public FlowOnly {
	//using clock = std::chrono::steady_clock;

	public: // config
		static constexpr float BETA {0.7f};
		static constexpr float SCALING_CONSTANT {0.4f};
		static constexpr float RTT_EMA_ALPHA = 0.1f; // 0.1 is very smooth, might need more

	private:
		// window size before last reduciton
		double _window_max {2.f * MAXIMUM_SEGMENT_SIZE}; // start with mss*2
		//double _window_last_max {2.f * MAXIMUM_SEGMENT_SIZE};
		double _time_point_reduction {getTimeNow()};

	private:
		float getCWnD(void) const;

		// moving avg over the last few delay samples
		// VERY sensitive to bundling acks
		//float getCurrentDelay(void) const;

		//void addRTT(float new_delay);

		void onCongestion(void) override;

	public: // api
		CUBIC(size_t maximum_segment_data_size) : FlowOnly(maximum_segment_data_size) {}

		// TODO: api for how much data we should send
		// take time since last sent into account
		// respect max_byterate_allowed
		size_t canSend(void) override;

		// get the list of timed out seq_ids
		//std::vector<SeqIDType> getTimeouts(void) const override;

	public: // callbacks
		// data size is without overhead
		//void onSent(SeqIDType seq, size_t data_size) override;

		//void onAck(std::vector<SeqIDType> seqs) override;

		// if discard, not resent, not inflight
		//void onLoss(SeqIDType seq, bool discard) override;
};

