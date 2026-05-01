#pragma once

#include <cstddef>
#include <cstdlib>

struct StagedEMA {
	const float ALPHA {0.01f}; // weight of the new slice
	const float ALPHA2 {0.25f}; // weight of the new slice
	float _avg {0.f}; // running exponential moving avg
	float _avg2 {0.f}; // smoothed input
	float _dev {0.f}; // deviation, kinda

	float _stage_sum {0.f};
	float _stage_dev_cheap_sum {0.f};
	size_t _stage_count {0};
	float _stage_time {0.f};

	void addValue(const float value) {
		_stage_sum += value;
		_stage_dev_cheap_sum += std::abs(_avg - value);
		_stage_count++;
	}

	// callback is void(float avg, float avg2, float dev, float stage_avg, float state_dev, size_t stage_count)
	template<typename FN>
	void update(const float time_delta, const float stage_duration, const FN&& callback) {
		if (_stage_count <= 0) {
			return;
		}

		_stage_time += time_delta;
		if (_stage_time >= stage_duration) {
			const float new_avg = _stage_sum / _stage_count;
			const float new_dev = _stage_dev_cheap_sum / _stage_count;

			_avg2 = ALPHA2 * new_avg + (1.f - ALPHA2) * _avg2; // before
			if (!callback(_avg, _avg2, _dev, new_avg, new_dev, _stage_count)) {
				// reset the value, kinda
				_avg2 = _avg;
			}
			_avg = ALPHA * new_avg + (1.f - ALPHA) * _avg; // after
			_dev = ALPHA * new_dev + (1.f - ALPHA) * _dev;

			_stage_sum = 0.f;
			_stage_dev_cheap_sum = 0.f;
			_stage_count = 0;
			_stage_time = 0.f;
		}
	}
};
