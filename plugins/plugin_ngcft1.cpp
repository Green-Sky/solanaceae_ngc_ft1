#include <solanaceae/plugin/solana_plugin_v1.h>

#include <solanaceae/ngc_ext/ngcext.hpp>
#include <solanaceae/ngc_ft1/ngcft1.hpp>
#include <solanaceae/ngc_ft1_sha1/sha1_ngcft1.hpp>

#include <entt/entt.hpp>
#include <entt/fwd.hpp>

#include <memory>
#include <iostream>

static std::unique_ptr<NGCEXTEventProvider> g_ngcextep = nullptr;
// TODO: make sep plug
static std::unique_ptr<NGCFT1> g_ngcft1 = nullptr;
static std::unique_ptr<SHA1_NGCFT1> g_sha1_ngcft1 = nullptr;

constexpr const char* plugin_name = "NGCEXT";

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
		auto* os = PLUG_RESOLVE_INSTANCE(ObjectStore2);
		auto* tox_i = PLUG_RESOLVE_INSTANCE(ToxI);
		auto* tox_event_provider_i = PLUG_RESOLVE_INSTANCE(ToxEventProviderI);
		auto* cs = PLUG_RESOLVE_INSTANCE(ContactStore4I);
		auto* rmm = PLUG_RESOLVE_INSTANCE(RegistryMessageModelI);
		auto* tcm = PLUG_RESOLVE_INSTANCE(ToxContactModel2);

		// static store, could be anywhere tho
		// construct with fetched dependencies
		g_ngcextep = std::make_unique<NGCEXTEventProvider>(*tox_i, *tox_event_provider_i);
		g_ngcft1 = std::make_unique<NGCFT1>(*tox_i, *tox_event_provider_i, *g_ngcextep.get());
		g_sha1_ngcft1 = std::make_unique<SHA1_NGCFT1>(*os, *cs, *rmm, *g_ngcft1.get(), *tcm, *tox_event_provider_i, *g_ngcextep.get());

		// register types
		PLUG_PROVIDE_INSTANCE(NGCEXTEventProviderI, plugin_name, g_ngcextep.get());

		PLUG_PROVIDE_INSTANCE(NGCFT1EventProviderI, plugin_name, g_ngcft1.get());
		PLUG_PROVIDE_INSTANCE(NGCFT1, plugin_name, g_ngcft1.get());

		PLUG_PROVIDE_INSTANCE(SHA1_NGCFT1, plugin_name, g_sha1_ngcft1.get());
	} catch (const ResolveException& e) {
		std::cerr << "PLUGIN " << plugin_name << " " << e.what << "\n";
		return 2;
	}

	return 0;
}

SOLANA_PLUGIN_EXPORT void solana_plugin_stop(void) {
	std::cout << "PLUGIN " << plugin_name << " STOP()\n";

	g_sha1_ngcft1.reset();
	g_ngcft1.reset();
	g_ngcextep.reset();
}

SOLANA_PLUGIN_EXPORT float solana_plugin_tick(float delta) {
	const float ft_interval = g_ngcft1->iterate(delta);
	const float sha_interval = g_sha1_ngcft1->iterate(delta);
	return std::min<float>(ft_interval, sha_interval);
}

} // extern C

