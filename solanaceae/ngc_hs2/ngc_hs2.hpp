#pragma once

//#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/toxcore/tox_event_interface.hpp>

//#include <solanaceae/message3/registry_message_model.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

// fwd
class ToxContactModel2;

class NGCHS2 : public ToxEventI, public NGCFT1EventI {
	ToxContactModel2& _tcm;
	//Contact3Registry& _cr;
	//RegistryMessageModelI& _rmm;
	ToxEventProviderI::SubscriptionReference _tep_sr;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;

	public:
		NGCHS2(
			ToxContactModel2& tcm,
			ToxEventProviderI& tep,
			NGCFT1& nf
		);

		~NGCHS2(void);

		float iterate(float delta);

	protected:
		bool onEvent(const Events::NGCFT1_recv_request&) override;
		bool onEvent(const Events::NGCFT1_recv_init&) override;
		bool onEvent(const Events::NGCFT1_recv_data&) override;
		bool onEvent(const Events::NGCFT1_send_data&) override;
		bool onEvent(const Events::NGCFT1_recv_done&) override;
		bool onEvent(const Events::NGCFT1_send_done&) override;

	protected:
		bool onToxEvent(const Tox_Event_Group_Peer_Join* e) override;
		bool onToxEvent(const Tox_Event_Group_Peer_Exit* e) override;
};

