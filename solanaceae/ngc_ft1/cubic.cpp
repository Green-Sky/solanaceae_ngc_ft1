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
	if (getTimeNow() - _time_point_reduction >= getCurrentDelay()*4.f) {
		const auto current_cwnd = getCWnD();
		const auto tmp_old_tp = _time_point_reduction;
		_time_point_reduction = getTimeNow();
		_window_max = current_cwnd * BETA;
		_window_max = std::max(_window_max, 2.*MAXIMUM_SEGMENT_SIZE);

#if 1
		std::cout << "CONGESTION!"
			<< " cwnd_max:" << _window_max
			<< " pts:" << tmp_old_tp
			<< " rtt:" << getCurrentDelay()
			<< "\n"
		;
#endif
	}
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

