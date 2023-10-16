#include "./cubic.hpp"

#include <cmath>
#include <iostream>

float CUBIC::getCWnD(void) const {
	const double K = cbrt(
		(_window_max * (1. - BETA)) / SCALING_CONSTANT
	);

	const double time_since_reduction = getTimeNow() - _time_point_reduction;

	const double TK = time_since_reduction - K;

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
	if (getTimeNow() - _time_point_reduction >= getCurrentDelay()*4.f) {
		const auto tmp_old_tp = getTimeNow() - _time_point_reduction;

		const auto current_cwnd = getCWnD(); // TODO: remove, only used by logging?
		const auto current_wnd = getWindow(); // respects cwnd and fwnd

		_time_point_reduction = getTimeNow();

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

float CUBIC::getWindow(void) {
	return std::min<float>(getCWnD(), FlowOnly::getWindow());
}

int64_t CUBIC::canSend(void) {
	const auto fspace_pkgs = FlowOnly::canSend();

	if (fspace_pkgs == 0u) {
		return 0u;
	}

	const int64_t cspace_bytes = getCWnD() - _in_flight_bytes;
	if (cspace_bytes < MAXIMUM_SEGMENT_DATA_SIZE) {
		return 0u;
	}

	// limit to whole packets
	int64_t cspace_pkgs = (cspace_bytes / MAXIMUM_SEGMENT_DATA_SIZE) * MAXIMUM_SEGMENT_DATA_SIZE;

	return std::min(cspace_pkgs, fspace_pkgs);
}

