#pragma once

#include <solanaceae/object_store/object_store.hpp>
#include <entt/container/dense_set.hpp>

namespace Contact::Components {

struct FT1Participation {
	entt::dense_set<Object> participating;
};

} // Contact::Components

