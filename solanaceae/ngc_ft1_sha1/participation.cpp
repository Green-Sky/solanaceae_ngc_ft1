#include "./participation.hpp"

#include "./contact_components.hpp"
#include "./chunk_picker.hpp"

#include <iostream>

bool addParticipation(ContactHandle4 c, ObjectHandle o) {
	bool was_new {false};
	assert(static_cast<bool>(o));
	assert(static_cast<bool>(c));

	if (static_cast<bool>(o)) {
		const auto [_, inserted] = o.get_or_emplace<Components::SuspectedParticipants>().participants.emplace(c);
		was_new = inserted;
	}

	if (static_cast<bool>(c)) {
		const auto [_, inserted] = c.get_or_emplace<Contact::Components::FT1Participation>().participating.emplace(o);
		was_new = was_new || inserted;
	}

	//std::cout << "added " << (was_new?"new ":"") << "participant\n";

	return was_new;
}

void removeParticipation(ContactHandle4 c, ObjectHandle o) {
	assert(static_cast<bool>(o));
	assert(static_cast<bool>(c));

	if (static_cast<bool>(o) && o.all_of<Components::SuspectedParticipants>()) {
		o.get<Components::SuspectedParticipants>().participants.erase(c);
	}

	if (static_cast<bool>(c)) {
		if (c.all_of<Contact::Components::FT1Participation>()) {
			c.get<Contact::Components::FT1Participation>().participating.erase(o);
		}

		if (c.all_of<ChunkPicker>()) {
			c.get<ChunkPicker>().participating_unfinished.erase(o);
		}
	}

	//std::cout << "removed participant\n";
}

