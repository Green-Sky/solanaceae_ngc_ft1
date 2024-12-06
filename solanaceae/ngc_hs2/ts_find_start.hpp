#pragma once

#include <algorithm>
#include <cstdint>

#include <iostream>

// perform binary search to find the first message not newer than ts_start
template<typename View>
auto find_start_by_ts(const View& view, uint64_t ts_start) {
	std::cout << "!!!! starting msg ts search, ts_start:" << ts_start << "\n";

	// -> first value smaller than start ts
	auto res = std::lower_bound(
		view.begin(), view.end(),
		ts_start,
		[&view](const auto& a, const auto& b) {
			const auto& [a_comp] = view.get(a);
			return a_comp.ts > b; // > bc ts is sorted high to low?
		}
	);

	if (res != view.end()) {
		const auto& [ts_comp] = view.get(*res);
		std::cout << "!!!! first value not newer than start ts is " << ts_comp.ts << "\n";
	} else {
		std::cout << "!!!! no first value not newer than start ts\n";
	}
	return res;
}

