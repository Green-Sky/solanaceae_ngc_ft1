#pragma once

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/tox_contacts/components.hpp>
#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include "./receiving_transfers.hpp"

namespace Systems {

void chunk_picker_updates(
	Contact3Registry& cr,
	ObjectRegistry& os_reg,
	const entt::dense_map<Contact3, size_t>& peer_open_requests,
	const ReceivingTransfers& receiving_transfers,
	NGCFT1& nft, // TODO: remove this somehow
	const float delta
);

} // Systems

