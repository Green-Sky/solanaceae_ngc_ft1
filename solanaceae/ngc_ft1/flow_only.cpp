#include "./flow_only.hpp"

#include <cmath>
#include <cassert>
#include <iostream>
#include <algorithm>

float FlowOnly::getCurrentRTT(void) const {
	// below 1ms is useless
	//return std::clamp(_rtt_ema, 0.001f, RTT_MAX);
	// the current iterate rate min is 5ms
	return std::clamp(_rtt_ema, 0.005f, RTT_MAX);
}

void FlowOnly::addRTT(float new_delay) {
	// TODO: move to slowstart, once we have that
	if (!_first_rtt_received) {
		_rtt_ema = new_delay;
		_first_rtt_received = true;
		//std::cerr << "!!!!!!!!!!!!!!!!!!\n!!! inital rtt set to " << new_delay << "\n!!!!!!!!!!!!\n";
	}

	float ema_alpha = RTT_EMA_ALPHA;
	if (new_delay > _rtt_ema * RTT_UP_MAX) {
		// too large a jump up, lower weight
		ema_alpha *= 0.1f;
	}

	// lerp(new_delay, rtt_ema, 0.1)
	_rtt_ema = ema_alpha * new_delay + (1.f - ema_alpha) * _rtt_ema;
}

void FlowOnly::updateWindow(void) {
	const float current_delay {getCurrentRTT()};

	_fwnd = max_byterate_allowed * current_delay;
	//_fwnd *= 1.3f; // try do balance conservative algo a bit, current_delay

	_fwnd = std::max(_fwnd, 2.f * MAXIMUM_SEGMENT_DATA_SIZE);
}

void FlowOnly::updateCongestion(void) {
	updateWindow();
	const auto tmp_window = FlowOnly::getWindow();
	// packet window * 0.3
	// but atleast 4
	int32_t max_consecutive_events = std::clamp<int32_t>(
		(tmp_window/MAXIMUM_SEGMENT_DATA_SIZE) * 0.3f,
		4,
		50 // limit TODO: fix idle/time starved algo
	);
	// TODO: magic number

#if 0
	std::cout << "NGC_FT1 Flow: pkg out of order"
		<< " w:" << tmp_window
		<< " pw:" << tmp_window/MAXIMUM_SEGMENT_DATA_SIZE
		<< " coe:" << _consecutive_events
		<< " mcoe:" << max_consecutive_events
		<< "\n";
#endif

	if (_consecutive_events > max_consecutive_events) {
		//std::cout << "CONGESTION! NGC_FT1 flow: pkg out of order\n";
		onCongestion();

		// TODO: set _consecutive_events to zero?
	}
}

float FlowOnly::getWindow(void) const {
	return _fwnd;
}

int64_t FlowOnly::canSend(float time_delta) {
	// a time slice is ~4rtts
	_sa_reorders.update(time_delta, getCurrentRTT()*4.f, [this](float prev_avg, float stage_value, size_t stage_count) -> bool {
		if (stage_count < 2*4 * 4) { // TODO: good number?
			// too few values to have any confidence
			// TODO: increase time slice?
			return true;
		}
		if (stage_value > (prev_avg + 0.001f) * 1.20f) { // TODO: good number?
			// new value 20% higher than prev avg
			std::cerr << "!!!!! SA reorders >20% higher than avg (new:" << stage_value << " avg:" << prev_avg << " sc:" << stage_count << " st:" << getCurrentRTT()*4.f << ")\n";
			onCongestion();
		} else {
			std::cerr << "SA reorders (new:" << stage_value << " avg:" << prev_avg << " sc:" << stage_count << " st:" << getCurrentRTT()*4.f << ")\n";
		}
		return true; // always
	});

	updateWindow();

	int64_t fspace_bytes = _fwnd - _in_flight_bytes;
	if (fspace_bytes < MAXIMUM_SEGMENT_DATA_SIZE) {
		return 0u;
	}

	// TODO: borked somehow, eg 15 limit results in ~12
	// also limit to max sendrate per tick, which is usually smaller than window
	// this is mostly to prevent spikes on empty windows
	fspace_bytes = std::min<int64_t>({
		fspace_bytes,

		// slice window into time time_delta sized chunks and only allow 1.5 chunks sized per tick
		int64_t((1.5f * _fwnd) / time_delta + 0.5f),

		// similar, but without current delay in the equation (fallback)
		int64_t(1.2f * max_byterate_allowed * time_delta + 0.5f),
	});

	// limit to whole packets
	int64_t fspace_pkgs = (fspace_bytes / MAXIMUM_SEGMENT_DATA_SIZE) * MAXIMUM_SEGMENT_DATA_SIZE;

	// dither up proportionally
	if (_rng() % MAXIMUM_SEGMENT_SIZE < fspace_pkgs - fspace_bytes) {
		fspace_pkgs += MAXIMUM_SEGMENT_SIZE;
	}

	return fspace_pkgs;
}

std::vector<FlowOnly::SeqIDType> FlowOnly::getTimeouts(void) {
	std::vector<SeqIDType> list;
	list.reserve(_in_flight.size()/3); // we dont know, so we just guess

	// after 6 rtt delay, we trigger timeout
	const auto now_adjusted = getTimeNow() - getCurrentRTT()*6.f;

	for (auto& [seq, time_stamp, size, acc, _] : _in_flight) {
		if (now_adjusted > time_stamp) {
			// make it so timed out packets no longer count as in-flight
			if (acc) {
				acc = false;
				_in_flight_bytes -= size;
				assert(_in_flight_bytes >= 0);
			}
			list.push_back(seq);
		}
	}

	return list;
}

int64_t FlowOnly::inFlightCount(void) const {
	return _in_flight.size();
}

int64_t FlowOnly::inFlightBytes(void) const {
	return _in_flight_bytes;
}

void FlowOnly::onSent(SeqIDType seq, size_t data_size) {
	if constexpr (true) {
		size_t sum {0u};
		for (const auto& it : _in_flight) {
			assert(it.id != seq);
			if (it.accounted) {
				sum += it.bytes;
			}
		}
		assert(_in_flight_bytes == sum);
	}

	const auto& new_entry = _in_flight.emplace_back(
		FlyingBunch{
			seq,
			static_cast<float>(getTimeNow()),
			data_size + SEGMENT_OVERHEAD,
			true,
			false
		}
	);
	_in_flight_bytes += new_entry.bytes;

	_time_point_last_update = getTimeNow();
}

void FlowOnly::onAck(std::vector<SeqIDType> seqs) {
	if (seqs.empty()) {
		assert(false && "got empty list of acks???");
		return;
	}

	const auto now {getTimeNow()};

	_time_point_last_update = now;

	// first seq in seqs is the actual value, all extra are for redundency
	{ // skip in ack is congestion event
		// 1. look at primary ack of packet
		auto it = std::find_if(_in_flight.begin(), _in_flight.end(), [seq = seqs.front()](const auto& v) -> bool {
			return v.id == seq;
		});
		if (it != _in_flight.end() && !it->ignore) {
			// find first non ignore, it should be the expected
			auto first_it = std::find_if_not(_in_flight.begin(), _in_flight.end(), [](const auto& v) -> bool { return v.ignore; });

			if (first_it != _in_flight.cend() && it != first_it && !first_it->ignore) {
				// not next expected seq -> skip detected

				// TODO: remove consecutive and update
				_consecutive_events++;
				_sa_reorders.addValue(1.f);

				// only handle once
				it->ignore = true;
				first_it->ignore = true;

				updateCongestion();
			} else {
				// only mesure delay, if not a congestion
				addRTT(now - it->timestamp);
				_consecutive_events = 0;
				_sa_reorders.addValue(0.f);
			}
		} else { // TOOD: if ! ignore too
			_sa_reorders.addValue(0.f);
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
			return v.id == seq;
		});

		if (it == _in_flight.end()) {
			continue; // not found, ignore
		} else {
			//most_recent = std::max(most_recent, std::get<1>(*it));
			if (it->accounted) {
				_in_flight_bytes -= it->bytes;
				assert(_in_flight_bytes >= 0);
			}
			//_recently_acked_data += std::get<2>(*it);
			_in_flight.erase(it); // TODO: bundle using remove
		}
	}
}

bool FlowOnly::onLoss(SeqIDType seq, bool discard) {
	auto it = std::find_if(_in_flight.begin(), _in_flight.end(), [seq](const auto& v) -> bool {
		assert(!std::isnan(v.timestamp));
		return v.id == seq;
	});

	// we care about it still being there, when we do not discard
	if (it == _in_flight.end()) {
		if (!discard) {
			std::cerr << "FLOW seq not found!\n";
		}
		return false; // not found, ignore ??
	}

	//std::cerr << "FLOW loss\n";

	// "if data lost is not to be retransmitted"
	if (discard) {
		if (it->accounted) {
			_in_flight_bytes -= it->bytes;
			assert(_in_flight_bytes >= 0);
		}
		_in_flight.erase(it);
		if (_in_flight.empty()) {
			assert(_in_flight_bytes == 0);
		}
		return true;
	} else {
		// and not take into rtt
		it->timestamp = getTimeNow();
	}

	// usually after data arrived out-of-order/duplicate
	if (!it->ignore) {
		it->ignore = true; // only handle once

		// this is usually a safe indicator for congestion/maxed connection
		onCongestion();
	}

	return true;
}

