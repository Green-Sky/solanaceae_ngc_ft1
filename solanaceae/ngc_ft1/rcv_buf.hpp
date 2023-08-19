#pragma once

#include <vector>
#include <map>
#include <deque>
#include <cstdint>

struct RecvSequenceBuffer {
	struct RSBEntry {
		std::vector<uint8_t> data;
	};

	// sequence_id -> entry
	std::map<uint16_t, RSBEntry> entries;

	uint16_t next_seq_id {0};

	// list of seq_ids to ack, this is seperate bc rsbentries are deleted once processed
	std::deque<uint16_t> ack_seq_ids;

	void erase(uint16_t seq);

	// inflight chunks
	size_t size(void) const;

	void add(uint16_t seq_id, std::vector<uint8_t>&& data);

	bool canPop(void) const;

	std::vector<uint8_t> pop(void);

	// for acking, might be bad since its front
	std::vector<uint16_t> frontSeqIDs(size_t count = 5) const;
};

