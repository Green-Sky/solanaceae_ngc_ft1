#pragma once

#include <solanaceae/toxcore/tox_event_interface.hpp>

#include <solanaceae/contact/contact_model3.hpp>
#include <solanaceae/message3/registry_message_model.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

#include <entt/container/dense_map.hpp>

#include <solanaceae/util/span.hpp>

#include <vector>

// fwd
class ToxContactModel2;


struct TimeRangeRequest {
	uint64_t ts_start{0};
	uint64_t ts_end{0};
};

// TODO: move to own file
namespace Components {
	struct IncommingTimeRangeRequestQueue {
		std::vector<TimeRangeRequest> _queue;

		// we should remove/notadd queued requests
		// that are subsets of same or larger ranges
		void queueRequest(const TimeRangeRequest& new_request);
	};

	struct IncommingTimeRangeRequestRunning {
		struct Entry {
			TimeRangeRequest ir;
			std::vector<uint8_t> data; // trasfer data in memory
		};
		entt::dense_map<uint8_t, Entry> _list;
	};
} // Components

class NGCHS2Send : public RegistryMessageModelEventI, public NGCFT1EventI {
	Contact3Registry& _cr;
	RegistryMessageModelI& _rmm;
	ToxContactModel2& _tcm;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;

	float _iterate_heat {0.f};
	constexpr static float _iterate_cooldown {1.22f}; // sec

	// open/running info requests (by c)
	// comp on peer c

	// open/running info responses (by c)
	// comp on peer c

	// limit to 2 uploads per peer simultaniously
	// TODO: increase for prod (4?) or maybe even lower?
	// currently per type
	constexpr static size_t _max_parallel_per_peer {2};

	constexpr static bool _only_send_self_observed {true};
	constexpr static int64_t _max_time_into_past_default {60*15}; // s

	public:
		NGCHS2Send(
			Contact3Registry& cr,
			RegistryMessageModelI& rmm,
			ToxContactModel2& tcm,
			NGCFT1& nf
		);

		~NGCHS2Send(void);

		float iterate(float delta);

		void handleTimeRange(Contact3Handle c, const Events::NGCFT1_recv_request&);

	protected:
		bool onEvent(const Message::Events::MessageConstruct&) override;
		bool onEvent(const Message::Events::MessageUpdated&) override;
		bool onEvent(const Message::Events::MessageDestory&) override;

	protected:
		bool onEvent(const Events::NGCFT1_recv_request&) override;
		bool onEvent(const Events::NGCFT1_send_data&) override;
		bool onEvent(const Events::NGCFT1_send_done&) override;
};

