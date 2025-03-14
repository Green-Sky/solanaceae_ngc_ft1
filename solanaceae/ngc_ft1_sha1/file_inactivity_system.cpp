#include "./file_inactivity_system.hpp"

#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/file/file2.hpp>

#include "./components.hpp"

#include <iostream>

namespace Systems {

void file_inactivity(
	ObjectRegistry& os_reg,
	float current_time
) {
	std::vector<Object> to_close;
	size_t total {0};
	os_reg.view<Components::FT1File2>().each([&os_reg, &to_close, &total, current_time](Object ov, const Components::FT1File2& ft_f) {
		if (current_time - ft_f.last_activity_ts >= 30.f) {
			// after 30sec of inactivity
			to_close.push_back(ov);
		}
		total++;
	});

	if (!to_close.empty()) {
		std::cout << "SHA1_NGCFT1: closing " << to_close.size() << " out of " << total << " open files\n";
		os_reg.remove<Components::FT1File2>(to_close.cbegin(), to_close.cend());
	}
}

} // Systems

