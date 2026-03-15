#include "./contact_components_to_string.hpp"

#include "./contact_components.hpp"
#include "./chunk_picker.hpp"

#include <entt/entity/entity.hpp>
#include <entt/entity/handle.hpp>
#include <entt/entity/registry.hpp>

#include <string>

namespace Contact {

void registerNGCFT1SHA1Components2Str(ContactStore4I& cs) {
	cs.registerComponentToString(
		entt::type_id<Contact::Components::FT1Participation>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<Contact::Components::FT1Participation>();
			std::string str = std::to_string(comp.participating.size()) + " participants";
			if (verbose) {
				str += ":";
				for (const auto& obj : comp.participating) {
					str += std::to_string(entt::to_entity(obj)) + ",";
				}
			}
			return str;
		},
		"NGCFT1SHA1",
		"FT1Participation",
		entt::type_id<Contact::Components::FT1Participation>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<ChunkPickerUpdateTag>().hash(),
		+[](ContactHandle4, bool) -> std::string { return ""; },
		"NGCFT1SHA1",
		"ChunkPickerUpdateTag",
		entt::type_id<ChunkPickerUpdateTag>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<ChunkPickerTimer>().hash(),
		+[](ContactHandle4 c, bool) -> std::string {
			return std::to_string(c.get<ChunkPickerTimer>().timer);
		},
		"NGCFT1SHA1",
		"ChunkPickerTimer",
		entt::type_id<ChunkPickerTimer>().name(),
		true
	);

	cs.registerComponentToString(
		entt::type_id<ChunkPicker>().hash(),
		+[](ContactHandle4 c, bool verbose) -> std::string {
			const auto& comp = c.get<ChunkPicker>();
			std::string str = "Unfinished: " + std::to_string(comp.participating_unfinished.size());

			if (comp.participating_in_last != entt::null) {
				str += " | Last: " + std::to_string(entt::to_entity(comp.participating_in_last));
			} else {
				str += " | Last: none";
			}

			if (verbose) {
				str += "\nMax TF Info: " + std::to_string(comp.max_tf_info_requests);
				str += " | Max TF Chunks: " + std::to_string(comp.max_tf_chunk_requests);
				// TODO: iterate?
				str += "\nEntries: " + std::to_string(comp.participating_unfinished.size());
			}

			return str;
		},
		"NGCFT1SHA1",
		"ChunkPicker",
		entt::type_id<ChunkPicker>().name(),
		true
	);
}

void unregisterNGCFT1SHA1Components2Str(ContactStore4I& cs) {
	cs.unregisterComponentToString(
		entt::type_id<Contact::Components::FT1Participation>().hash()
	);
	cs.unregisterComponentToString(
		entt::type_id<ChunkPickerUpdateTag>().hash()
	);
	cs.unregisterComponentToString(
		entt::type_id<ChunkPickerTimer>().hash()
	);
	cs.unregisterComponentToString(
		entt::type_id<ChunkPicker>().hash()
	);
}

} // Contact

