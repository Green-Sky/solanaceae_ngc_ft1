#include "./ts_find_start.hpp"

#include <solanaceae/message3/registry_message_model.hpp>
#include <solanaceae/message3/components.hpp>

#include <cassert>

int main(void) {
	Message3Registry msg_reg;

	{
		std::cout << "TEST empty reg\n";
		auto ts_view = msg_reg.view<Message::Components::Timestamp>();
		const auto res = find_start_by_ts(ts_view, 42);
		assert(res == ts_view.end());
	}

	{
		std::cout << "TEST single msg newer (fail)\n";
		Message3Handle msg{msg_reg, msg_reg.create()};
		msg.emplace<Message::Components::Timestamp>(43ul);

		auto ts_view = msg_reg.view<Message::Components::Timestamp>();
		const auto res = find_start_by_ts(ts_view, 42);
		assert(res == ts_view.end());

		msg.destroy();
	}

	{
		std::cout << "TEST single msg same (succ)\n";
		Message3Handle msg{msg_reg, msg_reg.create()};
		msg.emplace<Message::Components::Timestamp>(42ul);

		auto ts_view = msg_reg.view<Message::Components::Timestamp>();
		const auto res = find_start_by_ts(ts_view, 42);
		assert(res != ts_view.end());

		msg.destroy();
	}

	{
		std::cout << "TEST single msg older (succ)\n";
		Message3Handle msg{msg_reg, msg_reg.create()};
		msg.emplace<Message::Components::Timestamp>(41ul);

		auto ts_view = msg_reg.view<Message::Components::Timestamp>();
		const auto res = find_start_by_ts(ts_view, 42);
		assert(res != ts_view.end());

		msg.destroy();
	}

	{
		std::cout << "TEST multi msg\n";
		Message3Handle msg{msg_reg, msg_reg.create()};
		msg.emplace<Message::Components::Timestamp>(41ul);
		Message3Handle msg2{msg_reg, msg_reg.create()};
		msg2.emplace<Message::Components::Timestamp>(42ul);
		Message3Handle msg3{msg_reg, msg_reg.create()};
		msg3.emplace<Message::Components::Timestamp>(43ul);

		// see message3/message_time_sort.cpp
		msg_reg.sort<Message::Components::Timestamp>([](const auto& lhs, const auto& rhs) -> bool {
			return lhs.ts > rhs.ts;
		}, entt::insertion_sort{});

		auto ts_view = msg_reg.view<Message::Components::Timestamp>();
		auto res = find_start_by_ts(ts_view, 42);
		assert(res != ts_view.end());
		assert(*res == msg2);
		res++;
		assert(*res == msg);

		msg3.destroy();
		msg2.destroy();
		msg.destroy();
	}

	return 0;
}

