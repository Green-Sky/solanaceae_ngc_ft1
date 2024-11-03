#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/message3/registry_message_model.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include <entt/container/dense_map.hpp>

// fwd
class ToxContactModel2;

// limit to 2 uploads per peer simultaniously

class NGCHS2Send : public RegistryMessageModelEventI, public NGCFT1EventI {
	Contact3Registry& _cr;
	RegistryMessageModelI& _rmm;
	ToxContactModel2& _tcm;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;

	// open/running info requests (by c)

	// open/running info responses (by c)

	static const bool _only_send_self_observed {true};
	static const int64_t _max_time_into_past_default {60}; // s

	public:
		NGCHS2Send(
			Contact3Registry& cr,
			RegistryMessageModelI& rmm,
			ToxContactModel2& tcm,
			NGCFT1& nf
		);

		~NGCHS2Send(void);

		float iterate(float delta);

		void handleRange(Contact3Handle c, const Events::NGCFT1_recv_request&);
		void handleSingleMessage(Contact3Handle c, const Events::NGCFT1_recv_request&);

	protected:
		bool onEvent(const Message::Events::MessageConstruct&) override;
		bool onEvent(const Message::Events::MessageUpdated&) override;
		bool onEvent(const Message::Events::MessageDestory&) override;

	protected:
		bool onEvent(const Events::NGCFT1_recv_request&) override;
		bool onEvent(const Events::NGCFT1_send_data&) override;
		bool onEvent(const Events::NGCFT1_send_done&) override;
};

