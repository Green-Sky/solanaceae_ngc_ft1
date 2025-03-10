#pragma once

#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/contact/fwd.hpp>
#include <solanaceae/ngc_ext/ngcext.hpp>

namespace Systems {

void re_announce(
	ObjectRegistry& os_reg,
	ContactStore4I& cs,
	NGCEXTEventProvider& neep,
	const float delta
);

} // Systems

