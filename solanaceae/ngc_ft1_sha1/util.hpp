#pragma once

#include <cstdint>

inline static uint64_t combine_ids(const uint32_t group_number, const uint32_t peer_number) {
	return (uint64_t(group_number) << 32) | peer_number;
}

inline static void decompose_ids(const uint64_t combined_id, uint32_t& group_number, uint32_t& peer_number) {
	group_number = combined_id >> 32;
	peer_number = combined_id & 0xffffffff;
}

