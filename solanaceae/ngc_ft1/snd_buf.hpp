#pragma once

#include <vector>
#include <map>
#include <cstdint>

struct SendSequenceBuffer {
	struct SSBEntry {
		std::vector<uint8_t> data; // the data (variable size, but smaller than 500)
		float time_since_activity {0.f};
	};

	// sequence_id -> entry
	std::map<uint16_t, SSBEntry> entries;

	uint16_t next_seq_id {0};

	void erase(uint16_t seq);

	// inflight chunks
	size_t size(void) const;

	uint16_t add(std::vector<uint8_t>&& data);

	template<typename FN>
	void for_each(float time_delta, FN&& fn) {
		for (auto& [id, entry] : entries) {
			entry.time_since_activity += time_delta;
			fn(id, entry.data, entry.time_since_activity);
		}
	}
};

