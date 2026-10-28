#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

namespace RE
{
	struct ButtonEvent
	{
		float value, seconds;
		unsigned key = 18;
		int GetDevice() const { return 0; }
		unsigned GetIDCode() const { return key; }
		float HeldDuration() const { return seconds; }
		bool IsDown() const { return value > 0 && seconds == 0; }
		bool IsPressed() const { return value > 0; }
		bool IsHeld() const { return value > 0 && seconds > 0; }
		bool IsUp() const { return value == 0 && seconds > 0; }
	};
	struct ControlMap
	{
		static ControlMap* GetSingleton()
		{
			static ControlMap map;
			return &map;
		}
		unsigned GetMappedKey(const char*, int) const { return 18; }
	};
	struct Setting
	{
		float GetFloat() const { return .3f; }
	};
}
namespace REL
{
	struct Module
	{
		static bool IsVR() { return false; }
	};
}
namespace QuickLoot
{
	struct LootMenuManager
	{
		static inline bool showing = true;
		static bool IsShowing() { return showing; }
	};
	namespace Input
	{
		constexpr float holdTimeThreshold = .4f, initialRetriggerDelay = .5f, subsequentRetriggerDelay = .05f;
		struct DeviceKey
		{
			int device;
			unsigned key;
			auto operator<=>(const DeviceKey&) const = default;
		};
		DeviceKey NormalizeDeviceKey(int device, unsigned key) { return { device, key }; }
		enum class KeybindingFlags
		{
			kNone = 0,
			kGlobal = 1,
			kRetrigger = 2,
			kOnRelease = 4,
			kOnHold = 8
		};
		struct Flags
		{
			KeybindingFlags value{};
			bool all(KeybindingFlags flag) const { return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) == static_cast<unsigned>(flag); }
			bool none(KeybindingFlags flag) const { return !all(flag); }
			bool all(KeybindingFlags a, KeybindingFlags b) const { return all(a) && all(b); }
			bool none(KeybindingFlags a, KeybindingFlags b) const { return !all(a) && !all(b); }
		};
		struct Keybinding
		{
			DeviceKey inputKey{ 0, 18 };
			Flags flags;
			bool isModifierSatisfied = true;
			float nextRetriggerTime = 0;
			int action = 0;
		};
		struct InputManager
		{
			static inline std::vector<Keybinding> _keybindings;
			static inline std::set<DeviceKey> _allInputKeys, _allModifierKeys, _sessionPressedKeys;
			static inline bool _triggerOnActivateRelease = false, grabSucceeds = false;
			static inline bool _activationPressValid = false;
			static inline RE::Setting setting;
			static inline RE::Setting* _grabDelaySetting = &setting;
			static inline int actions = 0, grabs = 0, modifierRefreshes = 0, lastAction = -1;
			static bool TryGrab()
			{
				++grabs;
				return grabSucceeds;
			}
			static void TriggerKeybinding(const Keybinding* binding)
			{
				++actions;
				lastAction = binding->action;
			}
			static void UpdateModifierStates() { ++modifierRefreshes; }
#ifdef QUICKLOOT_BASELINE_TEST
			static bool HandleGrab(const RE::ButtonEvent*, const Keybinding*);
#else
			static bool HandleGrab(const RE::ButtonEvent*, Keybinding*);
			static void ResetActionState();
			static void ResetTransientState();
#endif
			static void HandleHold(const RE::ButtonEvent*, Keybinding*);
			static void HandleButtonEvent(const RE::ButtonEvent*);
			static Keybinding* FindSatisfiedKeybinding(DeviceKey, float);
		};
		// Generated verbatim from production methods by the test runner.
#include "activation_methods.inc"
	}
}
using QuickLoot::Input::InputManager;
using QuickLoot::Input::Keybinding;
using QuickLoot::Input::KeybindingFlags;

static void require(bool condition, const char* description)
{
	if (!condition) {
		std::cerr << "FAIL: " << description << '\n';
		std::exit(1);
	}
	std::cout << "PASS: " << description << '\n';
}
static void key(float value, float duration)
{
	RE::ButtonEvent event{ value, duration };
	InputManager::HandleButtonEvent(&event);
}
static void setup(bool includeTap = true, bool holdEnabled = true)
{
	InputManager::_keybindings.clear();
	InputManager::_allInputKeys = { { 0, 18 }, { 0, 19 } };
	InputManager::_allModifierKeys.clear();
	InputManager::_sessionPressedKeys.clear();
	if (includeTap)
		InputManager::_keybindings.push_back(Keybinding{ { 0, 18 }, { KeybindingFlags::kNone }, true, 0, 1 });
	InputManager::_keybindings.push_back(Keybinding{ { 0, 18 }, { KeybindingFlags::kOnHold }, holdEnabled, 0, 2 });
	InputManager::actions = InputManager::grabs = 0;
	InputManager::grabSucceeds = false;
}
int main()
{
	setup();
	key(1, 0);
	key(1, .2f);
	key(1, .4f);
	require(InputManager::actions == 1 && InputManager::lastAction == 2 && InputManager::grabs == 0, "explicit hold-Activate fires at threshold without starting a grab");
	key(1, .8f);
	key(0, .8f);
	require(InputManager::actions == 1, "held action does not repeat or fire again on release");
	key(1, 0);
	key(1, .4f);
	key(0, 0);
	require(InputManager::actions == 2, "fresh press and zero-duration release reset the hold timer");
	setup();
	key(1, 0);
	key(0, .1f);
	require(InputManager::actions == 1 && InputManager::lastAction == 1, "short Activate still uses its normal release action");
	setup(true, false);
	InputManager::grabSucceeds = true;
	key(1, 0);
	key(1, .31f);
	key(0, .5f);
	require(InputManager::grabs == 1 && InputManager::actions == 0, "unsatisfied hold modifier preserves normal object grabbing");
	setup(false);
	key(1, 0);
	key(0, .1f);
	require(!InputManager::_triggerOnActivateRelease && InputManager::actions == 0, "release without a satisfied action clears the activation latch");
#ifndef QUICKLOOT_BASELINE_TEST
	setup(false);
	InputManager::_keybindings[0].inputKey = { 0, 19 };
	auto rkey = [](float value, float duration) {
		RE::ButtonEvent event{ value, duration, 19 };
		InputManager::HandleButtonEvent(&event);
	};
	rkey(1, 0);
	rkey(1, .4f);
	require(InputManager::actions == 1, "ordinary hold-R fires once in its original session");
	InputManager::ResetActionState();
	rkey(1, .8f);
	rkey(0, .8f);
	require(InputManager::actions == 1, "hold-R cannot rearm on a replacement container while still pressed");
	rkey(1, 0);
	rkey(1, .4f);
	rkey(0, .4f);
	require(InputManager::actions == 2, "hold-R accepts a fresh press in the replacement session");
	setup();
	key(1, 0);
	InputManager::ResetActionState();
	key(0, .1f);
	require(InputManager::actions == 0, "tap pressed in an old session cannot act on a replacement container");
	setup();
	key(1, 0);
	InputManager::ResetActionState();
	key(1, .5f);
	key(0, .5f);
	require(InputManager::actions == 0 && InputManager::grabs == 0, "held Activate also waits for a fresh press after session replacement");
	setup(false);
	QuickLoot::LootMenuManager::showing = false;
	InputManager::_keybindings[0].inputKey = { 0, 19 };
	InputManager::_keybindings[0].flags = { KeybindingFlags::kGlobal };
	rkey(1, 0);
	require(InputManager::actions == 1, "global shortcut remains available without a loot session");
	InputManager::_keybindings[0].nextRetriggerTime = std::numeric_limits<float>::infinity();
	InputManager::ResetActionState();
	require(InputManager::_keybindings[0].nextRetriggerTime == std::numeric_limits<float>::infinity(), "ordinary session reset does not rearm held global shortcuts");
	QuickLoot::LootMenuManager::showing = true;
	InputManager::_triggerOnActivateRelease = true;
	InputManager::_activationPressValid = true;
	InputManager::_keybindings[0].nextRetriggerTime = std::numeric_limits<float>::infinity();
	InputManager::ResetTransientState();
	require(!InputManager::_triggerOnActivateRelease && !InputManager::_activationPressValid && InputManager::_keybindings[0].nextRetriggerTime == 0 && InputManager::modifierRefreshes == 1, "load cleanup clears activation latch, timers and modifier state");
#endif
	return 0;
}
