#include "./ui.hpp"

#include <solanaceae/contact/contact_store_i.hpp>
#include <solanaceae/contact/components.hpp>
#include <solanaceae/tox_contacts/components.hpp>

#include <solanaceae/tox_contacts/tox_contact_model2.hpp>

#include <entt/entity/registry.hpp>
#include <entt/entity/handle.hpp>

#include <imgui.h>

#include <stdexcept>

static void renderTab(ContactHandle4 c) {
	if (!c.all_of<Contact::Components::ToxGroupEphemeral>()) {
		return;
	}

	assert(c.registry()->ctx().contains<NGCFT1UI>());
	c.registry()->ctx().get<NGCFT1UI>().renderTabGroup(c);
}

NGCFT1UI::NGCFT1UI(NGCFT1& ft, ContactStore4I& cs, ToxContactModel2& tcm) : _ft(ft), _cs(cs), _tcm(tcm) {
	if (!_cs.registerImGuiChatTab(
		entt::type_id<Contact::Components::ToxGroupEphemeral>().hash(),
		renderTab
	)) {
		throw std::runtime_error("failed to register imgui chat tab");
	}

	_cs.registry().ctx().emplace<NGCFT1UI&>(*this);
}

NGCFT1UI::~NGCFT1UI(void) {
	_cs.unregisterImGuiChatTab(
		entt::type_id<Contact::Components::ToxGroupEphemeral>().hash()
	);

	_cs.registry().ctx().erase<NGCFT1UI&>();
}

void NGCFT1UI::render(float time_delta) {
	// TODO: remove?
	// TODO: inspection window with single group peer?
	//if (ImGui::Begin("ngcft test window")) {
	//    ImGui::Text("hi");
	//}
	//ImGui::End();
}

void NGCFT1UI::renderTabGroup(ContactHandle4 c) {
	const auto group_number = c.get<Contact::Components::ToxGroupEphemeral>().group_number;

	if (_ft.groups.count(group_number) < 1) {
		return; // nothing happening
	}

	auto& ft_group = _ft.groups.at(group_number);

	if (!ImGui::BeginTabItem("NGCFT1", nullptr, (!ft_group.peers.empty() ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None))) {
		return;
	}
	if (!ImGui::BeginChild("tab_content")) {
		ImGui::EndChild(); // always
		return;
	}

	ImGui::Text("active peers: %zu", ft_group.peers.size());

	{ // ui here
		ImGui::SeparatorText("receiving");
		for (const auto& [peer_number, peer] : ft_group.peers) {
			const char* peer_name = "<unk>";
			if (const auto& pc = _tcm.getContactGroupPeer(group_number, peer_number); pc) {
				if (const auto* name_comp_ptr = pc.try_get<Contact::Components::Name>(); name_comp_ptr != nullptr) {
					peer_name = name_comp_ptr->name.c_str();
				}
			}
			ImGui::Text("peer %u (%s)", peer_number, peer_name);
			ImGui::Indent();

			for (size_t i = 0; i < peer.recv_transfers.size(); i++) {
				const auto& transfer_opt = peer.recv_transfers.at(i);
				if (!transfer_opt.has_value()) {
					continue;
				}

				const auto& transfer = transfer_opt.value();

				const bool color_red = transfer.timer > 1.5f && transfer.state != NGCFT1::Group::Peer::RecvTransfer::State::FINISHING;
				const bool color_yellow = transfer.state == NGCFT1::Group::Peer::RecvTransfer::State::FINISHING;

				if (color_red) {
					ImGui::PushStyleColor(ImGuiCol_Text, {1.f, 0.5f, 0.5f, 1.f});
				} else if (color_yellow) {
					ImGui::PushStyleColor(ImGuiCol_Text, {1.f, 1.0f, 0.5f, 1.f});
				}

				ImGui::Text("%zu %s kind:%u (%lu/%lu Bytes) t:%.3f",
					i,
					transfer.state == NGCFT1::Group::Peer::RecvTransfer::State::INITED ? "INITED" : (transfer.state == NGCFT1::Group::Peer::RecvTransfer::State::RECV ? "RECV" : "FINISHING"),
					transfer.file_kind,
					transfer.file_size_current,
					transfer.file_size,
					transfer.timer
				);

				//transfer.rsb.

				if (color_red || color_yellow) {
					ImGui::PopStyleColor();
				}
			}

			ImGui::Unindent();
		}

		ImGui::SeparatorText("sending");
		for (const auto& [peer_number, peer] : ft_group.peers) {
			const char* peer_name = "<unk>";
			if (const auto& pc = _tcm.getContactGroupPeer(group_number, peer_number); pc) {
				if (const auto* name_comp_ptr = pc.try_get<Contact::Components::Name>(); name_comp_ptr != nullptr) {
					peer_name = name_comp_ptr->name.c_str();
				}
			}
			ImGui::Text("peer %u (%s)", peer_number, peer_name);
			ImGui::Indent();

			const auto* cca = peer.cca.get();
			if (cca) {
				ImGui::TextUnformatted("cca:");
				ImGui::Indent();
				// TODO: human readable bytes
				// TODO: graphs
				ImGui::Text("iFB: %ld iFC: %ld delay: %.3f window: %.1f w/d: %.3fKiB/s",
					cca->inFlightBytes(),
					cca->inFlightCount(),
					cca->getCurrentDelay(),
					cca->getWindow(),
					(cca->getWindow()/cca->getCurrentDelay())/1024
				);
				ImGui::Unindent();
			}

			ImGui::Text("transfers:");
			ImGui::Indent();
			for (size_t i = 0; i < peer.send_transfers.size(); i++) {
				const auto& transfer_opt = peer.send_transfers.at(i);
				if (!transfer_opt.has_value()) {
					continue;
				}
				const auto& transfer = transfer_opt.value();

				ImGui::Text("%zu %s kind:%u (%lu/%lu Bytes) t:%.3f",
					i,
					transfer.state == NGCFT1::Group::Peer::SendTransfer::State::INIT_SENT ? "INIT_SENT" : (transfer.state == NGCFT1::Group::Peer::SendTransfer::State::SENDING ? "SENDING" : "FINISHING"),
					transfer.file_kind,
					transfer.file_size_current,
					transfer.file_size,
					transfer.time_since_activity
				);
			}
			ImGui::Unindent();

			ImGui::Unindent();
		}
	}

	ImGui::EndChild(); // always
	ImGui::EndTabItem();
}
