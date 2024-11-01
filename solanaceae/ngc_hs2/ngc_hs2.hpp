#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/message3/registry_message_model.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include <entt/container/dense_map.hpp>

// fwd
class ToxContactModel2;

// time ranges
//   should we just do last x minutes like zngchs?
//   properly done, we need to use:
//     - Message::Components::ViewCurserBegin
//     - Message::Components::ViewCurserEnd
//
//  on startup, manually check all registries for ranges (meh) (do later)
//  listen on message events, check if range, see if range satisfied recently
//  deal with a queue, and delay (at least 1sec, 3-10sec after a peer con change)
//  or we always overrequest (eg 48h), and only fetch messages in, or close to range

class NGCHS2 : public RegistryMessageModelEventI, public ToxEventI, public NGCFT1EventI {
	Contact3Registry& _cr;
	RegistryMessageModelI& _rmm;
	RegistryMessageModelI::SubscriptionReference _rmm_sr;
	ToxContactModel2& _tcm;
	ToxEventProviderI::SubscriptionReference _tep_sr;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;

	// describes our knowlage of a remote peer
	struct RemoteInfo {
		// list of all ppk+mid+ts they sent us (filtered by reqs, like range, ppk...)
		// with when it last sent a range? hmm
	};
	entt::dense_map<Contact3, RemoteInfo> _remote_info;

	// open/running info requests (by c)

	// open/running info responses (by c)

	static const bool _only_send_self_observed {true};
	static const int64_t _max_time_into_past_default {60}; // s

	public:
		NGCHS2(
			Contact3Registry& cr,
			RegistryMessageModelI& rmm,
			ToxContactModel2& tcm,
			ToxEventProviderI& tep,
			NGCFT1& nf
		);

		~NGCHS2(void);

		float iterate(float delta);

		// add to queue with timer
		// check and updates all existing cursers for giving reg in queue
		void enqueueWantCurser(Message3Handle m);

	protected:
		bool onEvent(const Message::Events::MessageConstruct&) override;
		bool onEvent(const Message::Events::MessageUpdated&) override;
		bool onEvent(const Message::Events::MessageDestory&) override;

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

