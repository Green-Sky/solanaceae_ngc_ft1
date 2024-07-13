#pragma once

#include <solanaceae/object_store/object_store.hpp>

namespace Systems {

// time only needs to be relative
void transfer_tally_update(ObjectRegistry& os_reg, const float time_now);

} // Systems

