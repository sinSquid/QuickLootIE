"""Compile the production menu manager against a small engine adapter; no game deployment."""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess

STUB = r'''
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
namespace logger { template<class... T> void info(T&&...) {} }
namespace RE {
struct TESObjectREFR { virtual ~TESObjectREFR() = default; };
struct Actor : TESObjectREFR {};
struct ObjectRefHandle {
    int id{};
    explicit ObjectRefHandle(int value=0):id(value) {}
    explicit operator bool() const { return id != 0; }
    bool operator==(const ObjectRefHandle&) const = default;
    void reset() { id=0; }
    std::shared_ptr<TESObjectREFR> get() const {
        static std::unordered_map<int,std::shared_ptr<TESObjectREFR>> refs;
        if (!id || id==99) return {};
        if (!refs.contains(id)) refs[id]=std::make_shared<TESObjectREFR>();
        return refs[id];
    }
};
struct PlayerCamera {
    static PlayerCamera* GetSingleton() { static PlayerCamera camera;return &camera; }
    bool IsInThirdPerson() const { return true; }
};
enum class UI_MESSAGE_TYPE { kShow,kHide };
struct UI {
    static UI* GetSingleton() { static UI ui;return &ui; }
    bool IsMenuOpen(std::string_view) const { return false; }
};
struct UIMessageQueue {
    static inline bool available=true;
    static inline std::vector<UI_MESSAGE_TYPE> messages;
    static UIMessageQueue* GetSingleton() { static UIMessageQueue q;return available?&q:nullptr; }
    void AddMessage(std::string_view,UI_MESSAGE_TYPE type,void*) { messages.push_back(type); }
};
}
template<class T> T skyrim_cast(RE::TESObjectREFR* object) { return dynamic_cast<T>(object); }
namespace SKSE { struct PluginDeclaration {
    static PluginDeclaration* GetSingleton() { static PluginDeclaration d;return &d; }
    const char* GetName() const { return "QuickLootIE"; }
}; }
namespace QuickLoot {
enum class RefreshFlags { kButtonBar };
namespace Input {
enum class QuickLootAction { kDisable,kEnable,kTake };
struct InputManager {
    static inline bool blocked=false;
    static inline int actionResets=0;
    static inline std::function<void()> onBlock;
    static void BlockConflictingInputs() {
        blocked=true;auto callback=std::exchange(onBlock,{});if(callback)callback();
    }
    static void UnblockConflictingInputs() { blocked=false; }
    static void ResetActionState() { ++actionResets; }
}; }
struct LootMenu {
    static constexpr std::string_view MENU_NAME="LootMenu";
    int shows{},refreshes{},lastIndex{-1};RE::ObjectRefHandle lastContainer;
    std::function<void()> onShow;
    void Show(const RE::ObjectRefHandle& ref,int index) {
        ++shows;lastContainer=ref;lastIndex=index;
        auto callback=std::exchange(onShow,{});if(callback)callback();
    }
    void QueueRefresh(RefreshFlags) { ++refreshes; }
    void OnInputAction(Input::QuickLootAction) {}
};
struct MenuVisibilityManager {
    static void DisableLootMenu(const std::string&) {}
    static void EnableLootMenu(const std::string&) {}
};
namespace Behaviors {
struct ActivationPrompt { static void Block() {} static void Unblock() {} };
struct ContainerAnimator { static void CloseContainer(const RE::ObjectRefHandle&) {} };
}
namespace API {
enum class HandleResult { kContinue,kCancel };
struct APIServer {
    static inline std::function<HandleResult(const RE::ObjectRefHandle&)> onOpening;
    static HandleResult DispatchOpeningLootMenuEvent(const RE::ObjectRefHandle& ref) {
        auto callback=std::exchange(onOpening,{});return callback?callback(ref):HandleResult::kContinue;
    }
}; }
namespace Integrations {
struct BetterThirdPersonSelection { static bool Is3DWidgetEnabled() { return false; } };
struct NPCsNamesDistributor { static void RevealName(RE::Actor*) {} };
}
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--baseline-ref', help='Run the common closed-session regression against a Git revision')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    work = args.build_dir.resolve()
    generated = work / 'generated'
    stubs = work / 'stubs'
    generated.mkdir(parents=True, exist_ok=True)
    stubs.mkdir(parents=True, exist_ok=True)
    (stubs / 'TestGame.h').write_text(STUB, encoding='utf-8')
    for name in ['LootMenu.h', 'MenuVisibilityManager.h', 'Input/Input.h', 'Input/InputManager.h',
                 'Behaviors/ActivationPrompt.h', 'Behaviors/ContainerAnimator.h',
                 'Integrations/APIServer.h', 'Integrations/BetterThirdPersonSelection.h',
                 'Integrations/NPCsNamesDistributor.h']:
        target = stubs / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#pragma once\n#include "TestGame.h"\n', encoding='utf-8')
    for name in ['LootMenuManager.cpp', 'LootMenuManager.h']:
        source = repo / 'src' / name
        data = subprocess.check_output(['git', '-C', str(repo), 'show', args.baseline_ref + ':src/' + name]) if args.baseline_ref else source.read_bytes()
        (generated / name).write_bytes(data)
        print(name + ' SHA256 ' + hashlib.sha256(data).hexdigest(), flush=True)
    dev = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat')
    if not dev.is_file():
        raise FileNotFoundError('Install/configure the local MSVC developer environment: ' + str(dev))
    methods_source = (subprocess.check_output(['git', '-C', str(repo), 'show', args.baseline_ref + ':src/Input/InputManager.cpp']).decode('utf-8')
                      if args.baseline_ref else (repo / 'src/Input/InputManager.cpp').read_text(encoding='utf-8-sig'))
    methods = []
    for name in ['HandleButtonEvent', 'HandleGrab', 'HandleHold', 'FindSatisfiedKeybinding'] + ([] if args.baseline_ref else ['ResetActionState', 'ResetTransientState']):
        match = re.search(r'(?m)^[ \t]*(?:bool|void|Keybinding\*) InputManager::' + name + r'\(', methods_source)
        if not match:
            raise ValueError('Production method missing: ' + name)
        brace = methods_source.index('{', match.start())
        depth, end = 1, brace + 1
        while depth:
            if methods_source[end] == '{': depth += 1
            if methods_source[end] == '}': depth -= 1
            end += 1
        methods.append(methods_source[match.start():end])
    (generated / 'activation_methods.inc').write_text('\n'.join(methods), encoding='utf-8')
    ui_source = (subprocess.check_output(['git', '-C', str(repo), 'show', args.baseline_ref + ':src/LootMenu.cpp']).decode('utf-8')
                 if args.baseline_ref else (repo / 'src/LootMenu.cpp').read_text(encoding='utf-8-sig'))
    ui_methods = []
    for name in ['PostCreate', 'AdvanceMovie']:
        match = re.search(r'(?m)^[ \t]*void LootMenu::' + name + r'\(', ui_source)
        if not match:
            raise ValueError('Production UI method missing: ' + name)
        brace = ui_source.index('{', match.start())
        depth, end = 1, brace + 1
        while depth:
            if ui_source[end] == '{': depth += 1
            if ui_source[end] == '}': depth -= 1
            end += 1
        ui_methods.append(ui_source[match.start():end])
    (generated / 'ui_methods.inc').write_text('\n'.join(ui_methods), encoding='utf-8')
    executable = work / 'menu_recovery_tests.exe'
    hold_executable = work / 'activation_hold_tests.exe'
    ui_executable = work / 'ui_failure_tests.exe'
    define = ' /DQUICKLOOT_BASELINE_TEST' if args.baseline_ref else ''
    command = ('@echo off\ncall "' + str(dev) + '" -arch=x64 >nul\nif errorlevel 1 exit /b 1\n'
               'cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8' + define +
               ' /FI"' + str(stubs / 'TestGame.h') + '" /I"' + str(stubs) + '" /I"' + str(generated) + '"'
               ' "' + str(generated / 'LootMenuManager.cpp') + '" "' + str(repo / 'tests/menu_recovery_tests.cpp') + '"'
               ' /Fe:"' + str(executable) + '"\nif errorlevel 1 exit /b 1\n'
               'cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8' + define + ' /I"' + str(generated) + '"'
               ' "' + str(repo / 'tests/activation_hold_tests.cpp') + '" /Fe:"' + str(hold_executable) + '"\nif errorlevel 1 exit /b 1\n'
               'cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I"' + str(generated) + '"'
               ' "' + str(repo / 'tests/ui_failure_tests.cpp') + '" /Fe:"' + str(ui_executable) + '"\nexit /b %errorlevel%\n')
    batch = work / 'build-tests.cmd'
    batch.write_text(command, encoding='utf-8')
    subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=work, check=True)
    results = [subprocess.run([str(program)], cwd=work).returncode for program in [executable, hold_executable, ui_executable]]
    raise SystemExit(1 if any(results) else 0)


if __name__ == '__main__':
    main()
