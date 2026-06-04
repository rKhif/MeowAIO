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

namespace Features::Champions::Annie {
	namespace Menu {
		inline MenuSDK::CheckBox* ComboQ = nullptr;
		inline MenuSDK::CheckBox* ComboW = nullptr;
		inline MenuSDK::CheckBox* ComboE = nullptr;
		inline MenuSDK::CheckBox* ComboR = nullptr;
		inline MenuSDK::MultiList* ModeR = nullptr;
		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseROn;

		inline MenuSDK::CheckBox* HarassQ = nullptr;
		inline MenuSDK::CheckBox* HarassW = nullptr;

		inline MenuSDK::CheckBox* FarmQ = nullptr;

		inline MenuSDK::CheckBox* DrawQ = nullptr;
		inline MenuSDK::CheckBox* DrawW = nullptr;
		inline MenuSDK::CheckBox* DrawE = nullptr;
		inline MenuSDK::CheckBox* DrawR = nullptr;
	}

	void Initialize ();
	void Destroy ();
}
