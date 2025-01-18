#include "./rcv_buf.hpp"

#include <cassert>

void RecvSequenceBuffer::erase(uint16_t seq) {
	entries.erase(seq);
}

// inflight chunks
size_t RecvSequenceBuffer::size(void) const {
	return entries.size();
}

void RecvSequenceBuffer::add(uint16_t seq_id, std::vector<uint8_t>&& data) {
	entries[seq_id] = {data};
	ack_seq_ids.push_back(seq_id);
	if (ack_seq_ids.size() > 3) { // TODO: magic
		ack_seq_ids.pop_front();
	}
}

bool RecvSequenceBuffer::canPop(void) const {
	return entries.count(next_seq_id);
}

std::vector<uint8_t> RecvSequenceBuffer::pop(void) {
	assert(canPop());
	auto tmp_data = std::move(entries.at(next_seq_id).data);
	erase(next_seq_id);
	next_seq_id++;
	return tmp_data;
}

// for acking, might be bad since its front
std::vector<uint16_t> RecvSequenceBuffer::frontSeqIDs(size_t count) const {
	std::vector<uint16_t> seq_ids;
	auto it = entries.cbegin();
	for (size_t i = 0; i < count && it != entries.cend(); i++, it++) {
		seq_ids.push_back(it->first);
	}

	return seq_ids;
}

