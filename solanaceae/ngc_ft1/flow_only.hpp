#pragma once

#include "./cca.hpp"

#include <chrono>
#include <vector>
#include <tuple>

struct FlowOnly : public CCAI {
	protected:
		using clock = std::chrono::steady_clock;

	public: // config
		static constexpr float RTT_EMA_ALPHA = 0.001f; // might need change over time
		static constexpr float RTT_UP_MAX = 3.0f; // how much larger a delay can be to be taken into account
		static constexpr float RTT_MAX = 2.f; // 2 sec is probably too much

	protected:
		// initialize to low value, will get corrected very fast
		float _fwnd {0.01f * max_byterate_allowed}; // in bytes

		// rtt exponental moving average
		float _rtt_ema {0.1f};

		// list of sequence ids and timestamps of when they where sent (and payload size)
		struct FlyingBunch {
			SeqIDType id;
			float timestamp;
			size_t bytes;

			// set to true if counted as ce or resent due to timeout
			bool ignore {false};
		};
		std::vector<FlyingBunch> _in_flight;
		int64_t _in_flight_bytes {0};

		int32_t _consecutive_events {0};

		clock::time_point _time_start_offset;

		// used to clamp growth rate in the void
		double _time_point_last_update {getTimeNow()};

	protected:
		// make values relative to algo start for readability (and precision)
		// get timestamp in seconds
		double getTimeNow(void) const {
			return std::chrono::duration<double>{clock::now() - _time_start_offset}.count();
		}

		// moving avg over the last few delay samples
		// VERY sensitive to bundling acks
		float getCurrentDelay(void) const override;

		float getWindow(void) override;

		void addRTT(float new_delay);

		void updateWindow(void);

		virtual void onCongestion(void) {};

		// internal logic, calls the onCongestion() event
		void updateCongestion(void);

	public: // api
		FlowOnly(size_t maximum_segment_data_size) : CCAI(maximum_segment_data_size) {}
		virtual ~FlowOnly(void) {}

		// TODO: api for how much data we should send
		// take time since last sent into account
		// respect max_byterate_allowed
		int64_t canSend(float time_delta) override;

		// get the list of timed out seq_ids
		std::vector<SeqIDType> getTimeouts(void) const override;

		int64_t inFlightCount(void) const override;

	public: // callbacks
		// data size is without overhead
		void onSent(SeqIDType seq, size_t data_size) override;

		void onAck(std::vector<SeqIDType> seqs) override;

		// if discard, not resent, not inflight
		void onLoss(SeqIDType seq, bool discard) override;
};

