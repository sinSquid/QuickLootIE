#include "LootMenuManager.h"
#include <cstdlib>
#include <iostream>

using QuickLoot::LootMenuManager;
using QuickLoot::Input::InputManager;

static void require(bool condition, const char* description)
{
	if (!condition) {
		std::cerr << "FAIL: " << description << '\n';
		std::exit(1);
	}
	std::cout << "PASS: " << description << '\n';
}

int main()
{
	// A hardening fixture, not a reproduction of the reported Skyrim incident:
	// a different transition has left the custom input bit off while no menu is selected.
	InputManager::blocked = true;
	LootMenuManager::RequestHide();
	require(!InputManager::blocked, "closed-menu cleanup releases the owned input group");

#ifndef QUICKLOOT_BASELINE_TEST
	QuickLoot::LootMenu menu;
	const RE::ObjectRefHandle first{ 1 }, second{ 2 };
	const auto oldActionResets = InputManager::actionResets;
	LootMenuManager::RequestShow(first);
	LootMenuManager::RequestHide();
	require(InputManager::actionResets == oldActionResets + 2, "opening and closing both cancel actions from an older session");
	LootMenuManager::ProcessPendingTasks(menu);
	require(menu.shows == 0 && !InputManager::blocked, "hide invalidates an unprocessed show");

	LootMenuManager::RequestShow(first);
	LootMenuManager::ProcessPendingTasks(menu);
	LootMenuManager::RequestHide();
	LootMenuManager::RequestShow(second);
	LootMenuManager::OnMenuClosed();  // Previous Hide arrives before the newer Show is processed.
	require(LootMenuManager::IsShowing() && InputManager::blocked, "older UI close preserves a pending newer session");
	LootMenuManager::ProcessPendingTasks(menu);
	LootMenuManager::OnMenuClosed();  // External close of the now-visible second session.
	require(!LootMenuManager::IsShowing() && !InputManager::blocked, "external UI close releases the active session");

	menu.shows = menu.refreshes = 0;
	menu.onShow = [&] {
		menu.onShow = {};
		LootMenuManager::RequestHide();
		LootMenuManager::RequestShow(second);
	};
	LootMenuManager::RequestShow(first);
	LootMenuManager::RequestRefresh(QuickLoot::RefreshFlags::kButtonBar);
	LootMenuManager::ProcessPendingTasks(menu);
	require(menu.shows == 1 && menu.refreshes == 0, "already-drained old tasks are skipped after a reentrant close");
	LootMenuManager::ProcessPendingTasks(menu);
	require(menu.shows == 2 && menu.lastContainer == second, "newer session tasks remain available after old-task cancellation");
	LootMenuManager::RequestHide();

	InputManager::onBlock = [] { LootMenuManager::RequestHide(); };
	menu.shows = 0;
	LootMenuManager::RequestShow(first);
	LootMenuManager::ProcessPendingTasks(menu);
	require(!LootMenuManager::IsShowing() && !InputManager::blocked && menu.shows == 0, "input-callback reentry cannot resurrect a canceled show");

	QuickLoot::API::APIServer::onOpening = [&](const RE::ObjectRefHandle&) {
		QuickLoot::API::APIServer::onOpening = {};
		LootMenuManager::RequestShow(second);
		return QuickLoot::API::HandleResult::kCancel;
	};
	LootMenuManager::RequestShow(first);
	LootMenuManager::ProcessPendingTasks(menu);
	require(LootMenuManager::IsShowing() && menu.lastContainer == second && InputManager::blocked, "an older opening veto cannot close the replacement session");

	LootMenuManager::SaveLastSelectedIndex(second, 7);
	LootMenuManager::ResetForLoad();
	menu.shows = 0;
	LootMenuManager::ProcessPendingTasks(menu);
	require(!LootMenuManager::IsShowing() && !InputManager::blocked && menu.shows == 0, "load reset releases inputs and discards pending tasks");
	LootMenuManager::RequestShow(second);
	LootMenuManager::ProcessPendingTasks(menu);
	require(menu.lastIndex == 0, "load reset discards the old container selection index");
	LootMenuManager::RequestHide();

	RE::UIMessageQueue::available = false;
	LootMenuManager::RequestShow(first);
	require(!LootMenuManager::IsShowing() && !InputManager::blocked, "unavailable UI queue cannot leave capture active");
	RE::UIMessageQueue::available = true;
	LootMenuManager::RequestShow(first);
	LootMenuManager::RequestShow(RE::ObjectRefHandle{ 99 });
	require(!LootMenuManager::IsShowing() && !InputManager::blocked, "an unresolved container closes capture safely");
#endif
	return 0;
}
