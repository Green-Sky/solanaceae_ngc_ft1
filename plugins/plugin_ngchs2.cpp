#include <solanaceae/plugin/solana_plugin_v1.h>

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <solanaceae/ngc_ft1/ngcft1.hpp>
#include <solanaceae/ngc_ft1_sha1/sha1_ngcft1.hpp> // this hurts
#include <solanaceae/ngc_hs2/ngc_hs2_sigma.hpp>
#include <solanaceae/ngc_hs2/ngc_hs2_rizzler.hpp>

#include <entt/entt.hpp>
#include <entt/fwd.hpp>

#include <memory>
#include <iostream>

// https://youtu.be/OwT83dN82pc

static std::unique_ptr<NGCHS2Sigma> g_ngchs2s = nullptr;
static std::unique_ptr<NGCHS2Rizzler> g_ngchs2r = nullptr;

constexpr const char* plugin_name = "NGCHS2";

extern "C" {

SOLANA_PLUGIN_EXPORT const char* solana_plugin_get_name(void) {
	return plugin_name;
}

SOLANA_PLUGIN_EXPORT uint32_t solana_plugin_get_version(void) {
	return SOLANA_PLUGIN_VERSION;
}

SOLANA_PLUGIN_EXPORT uint32_t solana_plugin_start(struct SolanaAPI* solana_api) {
	std::cout << "PLUGIN " << plugin_name << " START()\n";

	if (solana_api == nullptr) {
		return 1;
	}

	try {
		//auto* tox_i = PLUG_RESOLVE_INSTANCE(ToxI);
		auto* tox_event_provider_i = PLUG_RESOLVE_INSTANCE(ToxEventProviderI);
		auto* cs = PLUG_RESOLVE_INSTANCE(ContactStore4I);
		auto* rmm = PLUG_RESOLVE_INSTANCE(RegistryMessageModelI);
		auto* tcm = PLUG_RESOLVE_INSTANCE(ToxContactModel2);
		auto* ngcft1 = PLUG_RESOLVE_INSTANCE(NGCFT1);
		auto* sha1_ngcft1 = PLUG_RESOLVE_INSTANCE(SHA1_NGCFT1);

		// static store, could be anywhere tho
		// construct with fetched dependencies
		g_ngchs2s = std::make_unique<NGCHS2Sigma>(*cs, *rmm, *tcm, *ngcft1);
		g_ngchs2r = std::make_unique<NGCHS2Rizzler>(*cs, *rmm, *tcm, *ngcft1, *tox_event_provider_i, *sha1_ngcft1);

		// register types
		PLUG_PROVIDE_INSTANCE(NGCHS2Sigma, plugin_name, g_ngchs2s.get());
		PLUG_PROVIDE_INSTANCE(NGCHS2Rizzler, plugin_name, g_ngchs2r.get());
	} catch (const ResolveException& e) {
		std::cerr << "PLUGIN " << plugin_name << " " << e.what << "\n";
		return 2;
	}

	return 0;
}

SOLANA_PLUGIN_EXPORT void solana_plugin_stop(void) {
	std::cout << "PLUGIN " << plugin_name << " STOP()\n";

	g_ngchs2r.reset();
	g_ngchs2s.reset();
}

SOLANA_PLUGIN_EXPORT float solana_plugin_tick(float delta) {
	const float sigma_interval = g_ngchs2s->iterate(delta);
	const float rizzler_interval = g_ngchs2r->iterate(delta);
	return std::min<float>(sigma_interval, rizzler_interval);
}

} // extern C

