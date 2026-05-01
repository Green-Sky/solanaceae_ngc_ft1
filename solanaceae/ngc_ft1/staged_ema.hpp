#pragma once

#include <cstddef>

struct StagedEMA {
	const float ALPHA {0.01f}; // weight of the new slice
	float _avg {0.f}; // running exponential moving avg

	float _stage_sum {0.f};
	size_t _stage_count {0};
	float _stage_time {0.f};

	void addValue(const float value) {
		_stage_sum += value;
		_stage_count++;
	}

	// callback is bool(float prev_avg, float stage_value, size_t stage_count)
	template<typename FN>
	void update(const float time_delta, const float stage_duration, const FN&& callback) {
		if (_stage_count <= 0) {
			return;
		}

		_stage_time += time_delta;
		if (_stage_time >= stage_duration) {
			const float new_avg = _stage_sum / _stage_count;

			if (callback(_avg, new_avg, _stage_count)) {
				// only add new value if callback allows us
				_avg = ALPHA * new_avg + (1.f - ALPHA) * _avg;
			}

			_stage_sum = 0.f;
			_stage_count = 0;
			_stage_time = 0.f;
		}
	}
};
