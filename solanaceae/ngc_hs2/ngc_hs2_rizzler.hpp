#pragma once

#include <solanaceae/contact/contact_model3.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>

// fwd
class ToxContactModel2;
class RegistryMessageModelI;

class NGCHS2Rizzler : public ToxEventI, public NGCFT1EventI {
	Contact3Registry& _cr;
	RegistryMessageModelI& _rmm;
	ToxContactModel2& _tcm;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;

	// 5s-6s
	const float _delay_before_first_request_min {5.f};
	const float _delay_before_first_request_add {1.f};

	std::uniform_real_distribution<float> _rng_dist {0.0f, 1.0f};
	std::minstd_rand _rng;

	struct RequestQueueInfo {
		float delay; // const
		float timer;
		uint64_t sync_delta; //?
	};
	// request queue
	// c -> delay, timer
	std::map<Contact3, RequestQueueInfo> _request_queue;

	public:
		NGCHS2Rizzler(
			Contact3Registry& cr,
			RegistryMessageModelI& rmm,
			ToxContactModel2& tcm,
			NGCFT1& nf
		);

		~NGCHS2Rizzler(void);

		float iterate(float delta);

	protected:
		bool sendRequest(
			uint32_t group_number, uint32_t peer_number,
			uint64_t ts_start, uint64_t ts_end
		);

	protected:
		bool onEvent(const Events::NGCFT1_recv_init&) override;
		bool onEvent(const Events::NGCFT1_recv_data&) override;
		bool onEvent(const Events::NGCFT1_recv_done&) override;

	protected:
		bool onToxEvent(const Tox_Event_Group_Peer_Join* e) override;
};

