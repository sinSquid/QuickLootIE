#include "LootMenuManager.h"

#include "Behaviors/ActivationPrompt.h"
#include "Behaviors/ContainerAnimator.h"
#include "Input/InputManager.h"
#include "Integrations/APIServer.h"
#include "Integrations/BetterThirdPersonSelection.h"
#include "Integrations/NPCsNamesDistributor.h"
#include "LootMenu.h"
#include "MenuVisibilityManager.h"

namespace QuickLoot
{
	bool LootMenuManager::IsShowing()
	{
		std::scoped_lock lock{ _lock };
		return static_cast<bool>(_currentContainer);
	}

	void LootMenuManager::RequestShow(const RE::ObjectRefHandle& container)
	{
		if (!container || !container.get()) {
			RequestHide();
			return;
		}

		std::uint64_t observedGeneration;
		{
			std::scoped_lock lock{ _lock };
			if (container == _currentContainer) {
				return;
			}
			observedGeneration = _generation;
		}

		if (API::APIServer::DispatchOpeningLootMenuEvent(container) != API::HandleResult::kContinue) {
			logger::info("Opening was canceled by API subscriber");
			std::scoped_lock lock{ _lock };
			if (_generation == observedGeneration) {
				EndSession(true);
			}
			return;
		}

		std::scoped_lock lock{ _lock };
		if (_generation != observedGeneration) {
			return;
		}

		Behaviors::ContainerAnimator::CloseContainer(_currentContainer);
		if (_generation != observedGeneration) {
			return;
		}

		if (!RE::PlayerCamera::GetSingleton()->IsInThirdPerson() ||
			!Integrations::BetterThirdPersonSelection::Is3DWidgetEnabled()) {
			Behaviors::ActivationPrompt::Block();
		}

		if (const auto actor = skyrim_cast<RE::Actor*>(container.get().get())) {
			Integrations::NPCsNamesDistributor::RevealName(actor);
		}

		_currentContainer = container;
		const auto generation = ++_generation;
		_pendingOpen = true;
		_taskQueue.clear();
		Input::InputManager::ResetActionState();
		const auto index = container == _lastContainer ? _lastSelectedIndex : 0;

		// This may trigger another call to RequestShow, so make sure it happens after _currentContainer is set.
		Input::InputManager::BlockConflictingInputs();
		if (_generation != generation) {
			return;
		}

		EnsureOpen();

		QueueLootMenuTask([=](LootMenu& menu) {
			menu.Show(container, index);
		},
			true);
	}

	void LootMenuManager::RequestHide()
	{
		std::scoped_lock lock{ _lock };
		EndSession(true);
	}

	void LootMenuManager::EndSession(bool sendHideMessage)
	{
		const auto previous = _currentContainer;
		const auto generation = ++_generation;

		_currentContainer.reset();
		_pendingOpen = false;
		_taskQueue.clear();
		Input::InputManager::ResetActionState();
		Behaviors::ActivationPrompt::Unblock();
		Behaviors::ContainerAnimator::CloseContainer(previous);
		if (_generation != generation) {
			return;
		}

		// Repair the owned input group even if the logical menu was already closed.
		Input::InputManager::UnblockConflictingInputs();

		if (sendHideMessage && previous && _generation == generation) {
			if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(LootMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		}
	}

	void LootMenuManager::OnMenuClosed()
	{
		std::scoped_lock lock{ _lock };
		// An older queued Hide must not cancel a newer Show waiting on the UI thread.
		if (!_pendingOpen) {
			EndSession(false);
		}
	}

	void LootMenuManager::ResetForLoad()
	{
		std::scoped_lock lock{ _lock };
		EndSession(true);
		_lastContainer.reset();
		_lastSelectedIndex = 0;
	}

	void LootMenuManager::RequestRefresh(RefreshFlags flags)
	{
		QueueLootMenuTask([=](LootMenu& menu) {
			menu.QueueRefresh(flags);
		});
	}

	void LootMenuManager::OnInputAction(Input::QuickLootAction action)
	{
		//logger::trace("Input action {}", static_cast<int>(action));

		const auto plugin = SKSE::PluginDeclaration::GetSingleton();

		switch (action) {
		case Input::QuickLootAction::kDisable:
			MenuVisibilityManager::DisableLootMenu(std::string(plugin->GetName()));
			break;

		case Input::QuickLootAction::kEnable:
			MenuVisibilityManager::EnableLootMenu(std::string(plugin->GetName()));
			break;

		default:
			QueueLootMenuTask([=](LootMenu& menu) {
				menu.OnInputAction(action);
			});
			break;
		}
	}

	void LootMenuManager::EnsureOpen()
	{
		// Queue Show even if an old instance is still visible but already has a Hide pending.
		if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(LootMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		} else {
			EndSession(false);
		}
	}

	void LootMenuManager::SaveLastSelectedIndex(const RE::ObjectRefHandle& container, int selectedIndex)
	{
		std::scoped_lock lock{ _lock };
		if (container.get()) {
			_lastContainer = container;
			_lastSelectedIndex = selectedIndex;
		}
	}

	void LootMenuManager::ProcessPendingTasks(LootMenu& menu)
	{
		std::vector<PendingTask> tasks;

		{
			// Swap out the queue so queueing new tasks within another task
			// cannot reallocate the vector mid-iteration.
			std::scoped_lock lock{ _lock };
			tasks.swap(_taskQueue);
		}

		for (auto& task : tasks) {
			std::scoped_lock lock{ _lock };
			if (task.generation != _generation || !_currentContainer) {
				continue;
			}
			task.task(menu);
			if (task.opensMenu && task.generation == _generation) {
				_pendingOpen = false;
			}
		}
	}

	void LootMenuManager::QueueLootMenuTask(LootMenuTask task, bool opensMenu)
	{
		if (!task) {
			return;
		}

		std::scoped_lock lock{ _lock };
		if (_currentContainer) {
			_taskQueue.push_back({ _generation, std::move(task), opensMenu });
		}
	}
}
