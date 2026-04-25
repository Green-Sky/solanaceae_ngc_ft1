#pragma once

#include "./ngcft1.hpp"

#include <solanaceae/contact/fwd.hpp>

// fwd
class ToxContactModel2;

// ui inspection of the NGCFT1 object
class NGCFT1UI {
	NGCFT1& _ft;
	ContactStore4I& _cs;
	ToxContactModel2& _tcm;

	public:
		NGCFT1UI(NGCFT1& ft, ContactStore4I& cs, ToxContactModel2& tcm);
		~NGCFT1UI(void);

		void render(float time_delta);
		void renderTabGroup(ContactHandle4 c);
};
