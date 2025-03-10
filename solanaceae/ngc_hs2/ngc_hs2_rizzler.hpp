#pragma once

#include <solanaceae/contact/fwd.hpp>
#include <solanaceae/toxcore/tox_event_interface.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>
#include <solanaceae/ngc_ft1_sha1/sha1_ngcft1.hpp>

// fwd
class ToxContactModel2;
class RegistryMessageModelI;


class NGCHS2Rizzler : public ToxEventI, public NGCFT1EventI {
	ContactStore4I& _cs;
	RegistryMessageModelI& _rmm;
	ToxContactModel2& _tcm;
	NGCFT1& _nft;
	NGCFT1EventProviderI::SubscriptionReference _nftep_sr;
	ToxEventProviderI::SubscriptionReference _tep_sr;
	SHA1_NGCFT1& _sha1_nft;

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
	std::map<Contact4, RequestQueueInfo> _request_queue;

	// FIXME: workaround missing contact events
	// only used on peer exit (no, also used to quicken lookups)
	entt::dense_map<uint64_t, ContactHandle4> _tox_peer_to_contact;

	public:
		NGCHS2Rizzler(
			ContactStore4I& cs,
			RegistryMessageModelI& rmm,
			ToxContactModel2& tcm,
			NGCFT1& nft,
			ToxEventProviderI& tep,
			SHA1_NGCFT1& sha1_nft
		);

		~NGCHS2Rizzler(void);

		float iterate(float delta);

	protected:
		bool sendRequest(
			uint32_t group_number, uint32_t peer_number,
			uint64_t ts_start, uint64_t ts_end
		);

		void handleMsgPack(ContactHandle4 c, const std::vector<uint8_t>& data);

	protected:
		bool onEvent(const Events::NGCFT1_recv_init&) override;
		bool onEvent(const Events::NGCFT1_recv_data&) override;
		bool onEvent(const Events::NGCFT1_recv_done&) override;

	protected:
		bool onToxEvent(const Tox_Event_Group_Peer_Join* e) override;
};

