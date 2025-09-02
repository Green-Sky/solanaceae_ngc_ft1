#pragma once

#include "./flow_only.hpp"

struct CUBIC : public FlowOnly {
	public: // config
		static constexpr float BETA {0.8f};
		static constexpr float SCALING_CONSTANT {0.4f};
		static constexpr float RTT_EMA_ALPHA = 0.1f; // 0.1 is very smooth, might need more

	private:
		// window size before last reduciton
		double _window_max {2.f * MAXIMUM_SEGMENT_SIZE}; // start with mss*2
		//double _window_last_max {2.f * MAXIMUM_SEGMENT_SIZE};

		double _time_since_reduction {12.f}; // warm start

	private:
		void updateReductionTimer(float time_delta);
		void resetReductionTimer(void);

		float getCWnD(void) const;

		void onCongestion(void) override;

	public: // api
		CUBIC(size_t maximum_segment_data_size) : FlowOnly(maximum_segment_data_size) {}
		virtual ~CUBIC(void) {}

		float getWindow(void) const override;

		// takes time since last sent into account
		// respects max_byterate_allowed
		int64_t canSend(float time_delta) override;
};

