#pragma once

#include <solanaceae/object_store/fwd.hpp>

namespace Systems {

void file_inactivity(
	ObjectRegistry& os_reg,
	float current_time
);

} // Systems

