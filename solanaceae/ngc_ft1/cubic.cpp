#include "./cubic.hpp"

#include <cmath>
#include <iostream>

void CUBIC::updateReductionTimer(float time_delta) {
	const auto now {getTimeNow()};

	// only keep updating while the cca interaction is not too long ago
	// or simply when there are packets in flight
	// (you need space to resend timedout, which still use up pipe space)
	if (!_in_flight.empty() || now - _time_point_last_update <= getCurrentDelay()*4.f) {
		_time_since_reduction += time_delta;
	}
}

void CUBIC::resetReductionTimer(void) {
	_time_since_reduction = 0.f;
}

float CUBIC::getCWnD(void) const {
	const double K = cbrt(
		(_window_max * (1. - BETA)) / SCALING_CONSTANT
	);

	const double TK = _time_since_reduction - K;

	const double cwnd =
		SCALING_CONSTANT
		* TK * TK * TK // TK^3
		+ _window_max
	;

#if 0
	std::cout
		<< "K:" << K
		<< " ts:" << time_since_reduction
		<< " TK:" << TK
		<< " cwnd:" << cwnd
		<< " rtt:" << getCurrentDelay()
		<< "\n"
	;
#endif

	return std::max<float>(cwnd, 2.f * MAXIMUM_SEGMENT_SIZE);
}

void CUBIC::onCongestion(void) {
	// 8 is probably too much (800ms for 100ms rtt)
	if (_time_since_reduction >= getCurrentDelay()*4.f) {
		const auto tmp_old_tp = _time_since_reduction;

		const auto current_cwnd = getCWnD(); // TODO: remove, only used by logging?
		const auto current_wnd = getWindow(); // respects cwnd and fwnd

		resetReductionTimer();

		if (current_cwnd < _window_max) {
			// congestion before reaching the inflection point (prev window_max).
			// reduce to wnd*beta to be fair
			_window_max = current_wnd * BETA;
		} else {
			_window_max = current_wnd;
		}

		_window_max = std::max(_window_max, 2.0*MAXIMUM_SEGMENT_SIZE);

#if 1
		std::cout << "----CONGESTION!"
			<< " cwnd:" << current_cwnd
			<< " wnd:" << current_wnd
			<< " cwnd_max:" << _window_max
			<< " pts:" << tmp_old_tp
			<< " rtt:" << getCurrentDelay()
			<< "\n"
		;
#endif
	}
}

float CUBIC::getWindow(void) const {
	return std::min<float>(getCWnD(), FlowOnly::getWindow());
}

int64_t CUBIC::canSend(float time_delta) {
	const auto fspace_pkgs = FlowOnly::canSend(time_delta);

	updateReductionTimer(time_delta);

	if (fspace_pkgs == 0u) {
		std::cerr << "CUBIC: flow said 0\n";
		return 0u;
	}

	const auto window = getCWnD();
	int64_t cspace_bytes = window - _in_flight_bytes;
	if (cspace_bytes < MAXIMUM_SEGMENT_DATA_SIZE) {
		//std::cerr << "CUBIC: cspace < seg size\n";
		return 0u;
	}

	// also limit to max sendrate per tick, which is usually smaller than window
	// this is mostly to prevent spikes on empty windows
	const auto rate = window / getCurrentDelay();

	// we dont want this limit to fall below atleast 1 segment
	const int64_t max_bytes_per_tick = std::max<int64_t>(rate * time_delta + 0.5f, MAXIMUM_SEGMENT_SIZE);
	cspace_bytes = std::min<int64_t>(cspace_bytes, max_bytes_per_tick);

	// limit to whole packets
	int64_t cspace_pkgs = (cspace_bytes / MAXIMUM_SEGMENT_DATA_SIZE) * MAXIMUM_SEGMENT_DATA_SIZE;

	return std::min(cspace_pkgs, fspace_pkgs);
}

