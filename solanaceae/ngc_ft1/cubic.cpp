#include "./cubic.hpp"

#include <cmath>
#include <iostream>

float CUBIC::getCWnD(void) const {
	const double K = cbrt(
		(_window_max * (1. - BETA)) / SCALING_CONSTANT
	);

	const auto time_since_reduction = getTimeNow() - _time_point_reduction;

	const double TK = time_since_reduction - K;

	const double cwnd =
		SCALING_CONSTANT
		* TK * TK * TK // TK^3
		+ _window_max
	;

	std::cout
		<< "K:" << K
		<< " ts:" << time_since_reduction
		<< " TK:" << TK
		<< " cwnd:" << cwnd
		<< " rtt:" << getCurrentDelay()
		<< "\n"
	;

	return std::max<float>(cwnd, 2.f * MAXIMUM_SEGMENT_SIZE);
}

void CUBIC::onCongestion(void) {
	const auto current_cwnd = getCWnD();
	_time_point_reduction = getTimeNow();
	_window_max = current_cwnd;

	//std::cout << "CONGESTION!\n";
}

size_t CUBIC::canSend(void) {
	const auto flow_space = FlowOnly::canSend();

	if (flow_space == 0) {
		return 0;
	}

	const int64_t cspace = getCWnD() - _in_flight_bytes;
	if (cspace < MAXIMUM_SEGMENT_DATA_SIZE) {
		return 0u;
	}

	// limit to whole packets
	size_t space = std::ceil(cspace / MAXIMUM_SEGMENT_DATA_SIZE)
		* MAXIMUM_SEGMENT_DATA_SIZE;

	return std::min(space, flow_space);
}

