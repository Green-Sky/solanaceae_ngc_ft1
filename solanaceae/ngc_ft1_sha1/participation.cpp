#include "./participation.hpp"

#include "./chunk_picker.hpp"

bool addParticipation(Contact3Handle c, ObjectHandle o) {
	bool was_new {false};

	if (static_cast<bool>(o)) {
		const auto [_, inserted] = o.get_or_emplace<Components::SuspectedParticipants>().participants.emplace(c);
		was_new = inserted;
	}

	if (static_cast<bool>(c)) {
		const auto [_, inserted] = c.get_or_emplace<ChunkPicker>().participating.emplace(o);
		was_new = was_new || inserted;

		// TODO: if not have_all
		c.get_or_emplace<ChunkPicker>().participating_unfinished.emplace(o, ChunkPicker::ParticipationEntry{});
	}

	return was_new;
}

void removeParticipation(Contact3Handle c, ObjectHandle o) {
	if (static_cast<bool>(o) && o.all_of<Components::SuspectedParticipants>()) {
		o.get<Components::SuspectedParticipants>().participants.erase(c);
	}

	if (static_cast<bool>(c) && c.all_of<ChunkPicker>()) {
		c.get<ChunkPicker>().participating.erase(o);
		c.get<ChunkPicker>().participating_unfinished.erase(o);
	}
}

