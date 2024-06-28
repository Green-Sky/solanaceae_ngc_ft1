#pragma once

#include <solanaceae/object_store/object_store.hpp>
#include <solanaceae/contact/contact_model3.hpp>

bool addParticipation(Contact3Handle c, ObjectHandle o);
void removeParticipation(Contact3Handle c, ObjectHandle o);

