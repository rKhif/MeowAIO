#pragma once
#include "SDK.h"
#include "Menu.h"
#include "BaseSpell.h"
#include "OrbwalkerData.h"
#include "TargetSelector.h"
#include "Prediction.h"
#include "DamageLib.h"
#include "Graphics.h"
#include "ChampionShared.h"
#include "NavigationMesh.h"
#include <algorithm>

#include "DelayedActions.h"

namespace Features::Champions::Nami
{
	struct UnitE
	{
		AIBaseClient* Unit;
		MenuSDK::Menu* Menu;
		MenuSDK::CheckBox* Booleans[ 6 ];
		MenuSDK::Slider* Priority;

		bool IsValidForE() const
		{
			return !Unit->IsDead() && Unit->Distance( ObjectManager::GetPlayer() ) <= 800.f;
		}

		bool IsEnabled() const
		{
			return Booleans[ 0 ] && Booleans[ 0 ]->Value;
		}

		bool OnQ() const
		{
			return Booleans[ 1 ] && Booleans[ 1 ]->Value;
		}

		bool OnW() const
		{
			return Booleans[ 2 ] && Booleans[ 2 ]->Value;
		}

		bool OnE() const
		{
			return Booleans[ 3 ] && Booleans[ 3 ]->Value;
		}

		bool OnR() const
		{
			return Booleans[ 4 ] && Booleans[ 4 ]->Value;
		}

		bool OnAA() const
		{
			return Booleans[ 5 ] && Booleans[ 5 ]->Value;
		}

		int GetPriority() const
		{
			return Priority && Priority->Value;
		}
	};

	struct HealUnit;

	namespace Menu
	{
		inline MenuSDK::CheckBox* ComboQ = nullptr;
		inline MenuSDK::CheckBox* SlowQ = nullptr;
		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseQOn;
		// inline MenuSDK::CheckBox* StasisQ = nullptr;
		inline MenuSDK::CheckBox* GapCloseQ = nullptr;
		inline MenuSDK::List* HitchanceQ = nullptr;
		inline MenuSDK::CheckBox* ScripterQ = nullptr;

		inline MenuSDK::CheckBox* ComboW = nullptr;
		inline MenuSDK::CheckBox* BounceW = nullptr;
		inline MenuSDK::CheckBox* HealW = nullptr;
		inline MenuSDK::Slider* HealMana = nullptr;
		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseWOn;
		inline std::unordered_map<uint32_t, HealUnit> HealSettings;

		inline MenuSDK::CheckBox* UseE = nullptr;
		inline MenuSDK::CheckBox* OnlyComboE = nullptr;
		inline MenuSDK::SliderF* HumanizerE = nullptr;
		inline std::unordered_map<uint32_t, UnitE> UseESettings;

		inline MenuSDK::Slider* AmountR = nullptr;
		inline MenuSDK::List* HitchanceR = nullptr;
		inline MenuSDK::KeyBind* SemiR = nullptr;

		inline MenuSDK::CheckBox* CancelQ = nullptr;
		inline MenuSDK::Slider* CancelQImportance = nullptr;
		inline MenuSDK::CheckBox* CancelR = nullptr;
		inline MenuSDK::Slider* CancelRImportance = nullptr;
	}

	struct HealUnit
	{
		AIBaseClient* Client;
		MenuSDK::CheckBox* HealToggle;
		MenuSDK::Slider* HealPercentage;

		[[nodiscard]] bool CanHeal() const
		{
			return Client->IsAlly() && !Client->IsDead() && Client->Distance( ObjectManager::GetPlayer() ) <= 725.f && Client->GetHealthPercentage() <= HealPercentage->Value && ObjectManager::GetPlayer()->GetResourcePercentage() >= Menu::HealMana->Value && Menu::HealW->Value;
		}
	};

	inline TargetSelector::Instance* TS;
	inline float LastCastTime[4] = { 0.f, 0.f, 0.f, 0.f };
	inline DelayedActions NamiActions;

	void Initialize();
	void Destroy();
}
