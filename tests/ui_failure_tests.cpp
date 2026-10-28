#include <cstdint>
#include <cstdlib>
#include <iostream>
namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}
namespace RE
{
	struct GViewport
	{
		float aspectRatio = 0;
	};
}
struct MovieDefinition
{
	float GetWidth() const { return 1920; }
	float GetHeight() const { return 1080; }
};
struct Movie
{
	MovieDefinition definition;
	void GetViewport(RE::GViewport*) {}
	MovieDefinition* GetMovieDef() { return &definition; }
	void SetViewport(const RE::GViewport&) {}
};
struct MoviePointer
{
	bool present = false;
	int nullAccesses = 0;
	Movie movie;
	explicit operator bool() const { return present; }
	Movie* operator->()
	{
		if (!present)
			++nullAccesses;
		return &movie;
	}
};
namespace QuickLoot
{
	struct LootMenuManager
	{
		static inline int hides = 0, tasks = 0;
		static void RequestHide() { ++hides; }
		template <class T>
		static void ProcessPendingTasks(T&)
		{
			++tasks;
		}
	};
	struct UniversalMenu
	{
		static inline int advances = 0;
		static void AdvanceMovie(float, std::uint32_t) { ++advances; }
	};
	struct LootMenu : UniversalMenu
	{
		MoviePointer uiMovie;
		int setups = 0, transforms = 0, refreshes = 0;
		void SetupMenuNode() { ++setups; }
		void SetTransform() { ++transforms; }
		void Refresh() { ++refreshes; }
		void PostCreate();
		void AdvanceMovie(float, std::uint32_t);
	};
#include "ui_methods.inc"
}
static void require(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
	std::cout << "PASS: " << message << '\n';
}
int main()
{
	QuickLoot::LootMenu broken;
	broken.PostCreate();
	require(broken.uiMovie.nullAccesses == 0 && broken.setups == 0 && broken.transforms == 0, "VR post-create does not touch a failed movie or set up its native node");
	broken.AdvanceMovie(.016f, 1);
	require(QuickLoot::LootMenuManager::hides == 1 && broken.refreshes == 0 && QuickLoot::UniversalMenu::advances == 0, "failed movie frame closes capture without advancing invalid UI");
	QuickLoot::LootMenu valid;
	valid.uiMovie.present = true;
	valid.PostCreate();
	valid.AdvanceMovie(.016f, 1);
	require(valid.setups == 1 && valid.transforms == 1 && valid.refreshes == 1 && QuickLoot::UniversalMenu::advances == 1, "valid movie keeps native post-create and frame processing");
	REL::Module::vr = false;
	QuickLoot::LootMenu flat;
	flat.uiMovie.present = true;
	flat.PostCreate();
	require(flat.setups == 0 && flat.uiMovie.nullAccesses == 0, "flat runtime does not enter the VR post-create path");
}
