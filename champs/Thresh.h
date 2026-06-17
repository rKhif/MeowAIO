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
#include "OrbwalkerSDK.h"
#include "NavigationMesh.h"
#include <algorithm>

namespace Features::Champions::Thresh {
	namespace Menu {
		inline MenuSDK::CheckBox* ComboQ = nullptr;
		inline MenuSDK::CheckBox* ComboQ2 = nullptr;
		inline MenuSDK::CheckBox* SafeQ2 = nullptr;
		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseQOn;
		inline MenuSDK::List* HitchanceQ = nullptr;
		inline MenuSDK::Slider* MinQRange = nullptr;
		inline MenuSDK::Slider* MaxQRange = nullptr;
		inline MenuSDK::CheckBox* DrawQ = nullptr;
		inline MenuSDK::ColorPicker* ColorQ = nullptr;

		inline MenuSDK::CheckBox* ComboE = nullptr;
		inline MenuSDK::List* ModeE = nullptr;
		inline MenuSDK::KeyBind* ToggleE = nullptr;
		inline MenuSDK::CheckBox* ForceEQ = nullptr;
		inline MenuSDK::CheckBox* AllowEQ2 = nullptr;
		inline MenuSDK::CheckBox* DisableAA = nullptr;
		inline MenuSDK::CheckBox* DrawE = nullptr;
		inline MenuSDK::ColorPicker* ColorE = nullptr;

		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseWOn;
		inline MenuSDK::KeyBind* ThrowLantern = nullptr;
		inline MenuSDK::CheckBox* LanternQ2 = nullptr;
		inline MenuSDK::CheckBox* LanternDamage = nullptr;
		inline MenuSDK::Slider* LanternDamagePerc = nullptr;
		inline MenuSDK::CheckBox* LanternDisplacements = nullptr;
		inline MenuSDK::CheckBox* LanternGapCloser = nullptr;
		inline MenuSDK::CheckBox* LanternCC = nullptr;
		inline MenuSDK::Slider* LanternAmountOfEnemiesAroundAlly = nullptr;

		inline MenuSDK::CheckBox* ComboR = nullptr;

		inline MenuSDK::CheckBox* DrawMode = nullptr;
	}

	void Initialize ();
	void Destroy ();
}
