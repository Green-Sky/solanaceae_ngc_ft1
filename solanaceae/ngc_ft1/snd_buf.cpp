#include "./snd_buf.hpp"

void SendSequenceBuffer::erase(uint16_t seq) {
	entries.erase(seq);
}

// inflight chunks
size_t SendSequenceBuffer::size(void) const {
	return entries.size();
}

uint16_t SendSequenceBuffer::add(std::vector<uint8_t>&& data) {
	entries[next_seq_id] = {data, 0.f};
	return next_seq_id++;
}

