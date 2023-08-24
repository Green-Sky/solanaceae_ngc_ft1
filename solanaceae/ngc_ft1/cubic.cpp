#include "./cubic.hpp"

#include <cmath>
#include <iostream>

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

	std::cout << "K:" << K << " TK:" << TK << " cwnd:" << cwnd << " rtt:" << getCurrentDelay() << "\n";

	return cwnd;
}

float CUBIC::getCurrentDelay(void) const {
	return _rtt_ema;
}

void CUBIC::addRTT(float new_delay) {
	// lerp(new_delay, rtt_ema, 0.1)
	_rtt_ema = RTT_EMA_ALPHA * new_delay + (1.f - RTT_EMA_ALPHA) * _rtt_ema;
}

size_t CUBIC::canSend(void) const {
	return 0;
}

std::vector<CUBIC::SeqIDType> CUBIC::getTimeouts(void) const {
	return {};
}

void CUBIC::onSent(SeqIDType seq, size_t data_size) {
}

void CUBIC::onAck(std::vector<SeqIDType> seqs) {
}

void CUBIC::onLoss(SeqIDType seq, bool discard) {
}

