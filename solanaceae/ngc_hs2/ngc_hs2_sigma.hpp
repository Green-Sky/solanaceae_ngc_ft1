#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>

#include <solanaceae/contact/fwd.hpp>
#include <solanaceae/message3/registry_message_model.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include <entt/container/dense_map.hpp>

#include <solanaceae/util/span.hpp>

#include <vector>
#include <deque>

// fwd
class ToxContactModel2;


struct TimeRangeRequest {
	uint64_t ts_start{0};
	uint64_t ts_end{0};
};

class NGCHS2Sigma : public RegistryMessageModelEventI, public NGCFT1EventI {
	ContactStore4I& _cs;
	RegistryMessageModelI& _rmm;
	ToxContactModel2& _tcm;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;

	float _iterate_heat {0.f};
	constexpr static float _iterate_cooldown {1.22f}; // sec

	// open/running range requests (by c)
	// comp on peer c

	// open/running range responses (by c)
	// comp on peer c

	// limit to 2 uploads per peer simultaniously
	// TODO: increase for prod (4?) or maybe even lower?
	// currently per type
	constexpr static size_t _max_parallel_per_peer {2};

	constexpr static bool _only_send_self_observed {true};
	constexpr static int64_t _max_time_into_past_default {60*15}; // s

	// FIXME: workaround missing contact events
	// only used on peer exit (no, also used to quicken lookups)
	entt::dense_map<uint64_t, ContactHandle4> _tox_peer_to_contact;

	public:
		NGCHS2Sigma(
			ContactStore4I& cs,
			RegistryMessageModelI& rmm,
			ToxContactModel2& tcm,
			NGCFT1& nft
		);

		~NGCHS2Sigma(void);

		float iterate(float delta);

		void handleTimeRange(ContactHandle4 c, const Events::NGCFT1_recv_request&);

		// msg reg contact
		// time ranges
		[[nodiscard]] std::vector<uint8_t> buildChatLogFileRange(ContactHandle4 c, uint64_t ts_start, uint64_t ts_end);

	protected:
		bool onEvent(const Message::Events::MessageConstruct&) override;
		bool onEvent(const Message::Events::MessageUpdated&) override;
		bool onEvent(const Message::Events::MessageDestory&) override;

	protected:
		bool onEvent(const Events::NGCFT1_recv_request&) override;
		bool onEvent(const Events::NGCFT1_send_data&) override;
		bool onEvent(const Events::NGCFT1_send_done&) override;
};

