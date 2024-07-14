#pragma once

#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/ngc_ext/ngcext.hpp>

namespace Systems {

void re_announce(
	ObjectRegistry& os_reg,
	Contact3Registry& cr,
	NGCEXTEventProvider& neep,
	const float delta
);

} // Systems

