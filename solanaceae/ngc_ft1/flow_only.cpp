#include "./flow_only.hpp"

#include <cmath>
#include <cassert>
#include <iostream>
#include <algorithm>

float FlowOnly::getCurrentDelay(void) const {
	return std::min(_rtt_ema, RTT_MAX);
}

void FlowOnly::addRTT(float new_delay) {
	if (new_delay > _rtt_ema * RTT_UP_MAX) {
		// too large a jump up, to be taken into account
		return;
	}

	// lerp(new_delay, rtt_ema, 0.1)
	_rtt_ema = RTT_EMA_ALPHA * new_delay + (1.f - RTT_EMA_ALPHA) * _rtt_ema;
}

void FlowOnly::updateWindow(void) {
	const float current_delay {getCurrentDelay()};

	_fwnd = max_byterate_allowed * current_delay;
	//_fwnd *= 1.3f; // try do balance conservative algo a bit, current_delay

	_fwnd = std::max(_fwnd, 2.f * MAXIMUM_SEGMENT_DATA_SIZE);
}

int64_t FlowOnly::canSend(void) {
	if (_in_flight.empty()) {
		assert(_in_flight_bytes == 0);
		return MAXIMUM_SEGMENT_DATA_SIZE;
	}

	updateWindow();

	const int64_t fspace = _fwnd - _in_flight_bytes;
	if (fspace < MAXIMUM_SEGMENT_DATA_SIZE) {
		return 0u;
	}

	// limit to whole packets
	return (fspace / MAXIMUM_SEGMENT_DATA_SIZE) * MAXIMUM_SEGMENT_DATA_SIZE;
}

std::vector<FlowOnly::SeqIDType> FlowOnly::getTimeouts(void) const {
	std::vector<SeqIDType> list;

	// after 3 rtt delay, we trigger timeout
	const auto now_adjusted = getTimeNow() - getCurrentDelay()*3.f;

	for (const auto& [seq, time_stamp, size] : _in_flight) {
		if (now_adjusted > time_stamp) {
			list.push_back(seq);
		}
	}

	return list;
}

void FlowOnly::onSent(SeqIDType seq, size_t data_size) {
	if constexpr (true) {
		for (const auto& it : _in_flight) {
			assert(std::get<0>(it) != seq);
		}
	}

	_in_flight.push_back({seq, getTimeNow(), data_size + SEGMENT_OVERHEAD});
	_in_flight_bytes += data_size + SEGMENT_OVERHEAD;
	//_recently_sent_bytes += data_size + SEGMENT_OVERHEAD;
}

void FlowOnly::onAck(std::vector<SeqIDType> seqs) {
	if (seqs.empty()) {
		assert(false && "got empty list of acks???");
		return;
	}

	const auto now {getTimeNow()};

	// first seq in seqs is the actual value, all extra are for redundency
	{ // skip in ack is congestion event
		// 1. look at primary ack of packet
		auto it = std::find_if(_in_flight.begin(), _in_flight.end(), [seq = seqs.front()](const auto& v) -> bool {
			return std::get<0>(v) == seq;
		});
		if (it != _in_flight.end()) {
			if (it != _in_flight.begin()) {
				// not next expected seq -> skip detected

				// TODO: change expectations of next seq in order, so we dont trigger a flood of ce

				//std::cout << "CONGESTION out of order\n";
				onCongestion();
			} else {
				// only mesure delay, if not a congestion
				addRTT(now - std::get<1>(*it));
			}
		} else {
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#if 0
			// assume we got a duplicated packet
			std::cout << "CONGESTION duplicate\n";
			onCongestion();
#endif
		}
	}

	for (const auto& seq : seqs) {
		auto it = std::find_if(_in_flight.begin(), _in_flight.end(), [seq](const auto& v) -> bool {
			return std::get<0>(v) == seq;
		});

		if (it == _in_flight.end()) {
			continue; // not found, ignore
		} else {
			//most_recent = std::max(most_recent, std::get<1>(*it));
			_in_flight_bytes -= std::get<2>(*it);
			assert(_in_flight_bytes >= 0);
			//_recently_acked_data += std::get<2>(*it);
			_in_flight.erase(it);
		}
	}
}

void FlowOnly::onLoss(SeqIDType seq, bool discard) {
	auto it = std::find_if(_in_flight.begin(), _in_flight.end(), [seq](const auto& v) -> bool {
		assert(!std::isnan(std::get<1>(v)));
		return std::get<0>(v) == seq;
	});

	if (it == _in_flight.end()) {
		// error
		return; // not found, ignore ??
	}

	//std::cerr << "FLOW loss\n";

	// "if data lost is not to be retransmitted"
	if (discard) {
		_in_flight_bytes -= std::get<2>(*it);
		assert(_in_flight_bytes >= 0);
		_in_flight.erase(it);
	}

	// TODO: reset timestamp?
	// and not take into rtt

	// no ce, since this is usually after data arrived out-of-order/duplicate
}

