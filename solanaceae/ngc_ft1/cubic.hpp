#pragma once

#include "./flow_only.hpp"

struct CUBIC : public FlowOnly {
	public: // config
		static constexpr float BETA {0.8f};
		static constexpr float SCALING_CONSTANT {0.4f};

	private:
		// window size before last reduction
		double _window_max {double(MAXIMUM_SEGMENT_SIZE)}; // start with mss

		double _time_since_reduction {28.f}; // warm start

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

