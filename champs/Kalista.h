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

namespace Features::Champions::Kalista
{
	namespace Menu
	{
		inline MenuSDK::CheckBox* OrbMinions = nullptr;
		inline MenuSDK::CheckBox* RestrictOrb = nullptr;

		inline MenuSDK::CheckBox* ComboQ = nullptr;
		inline MenuSDK::MultiList* ModeQ = nullptr;
		inline MenuSDK::SliderF* BlockQ = nullptr;
		inline MenuSDK::CheckBox* DrawQ = nullptr;
		inline MenuSDK::ColorPicker* ColorQ = nullptr;

		inline MenuSDK::CheckBox* ComboE = nullptr;
		inline MenuSDK::CheckBox* SlowE = nullptr;
		inline MenuSDK::CheckBox* OnlySlowRefund = nullptr;
		inline MenuSDK::CheckBox* AllowAfterAAE = nullptr;
		inline MenuSDK::CheckBox* KillstealE = nullptr;
		inline MenuSDK::List* DamageDrawMode = nullptr;
		inline MenuSDK::CheckBox* DrawE = nullptr;
		inline MenuSDK::ColorPicker* ColorE = nullptr;

		inline MenuSDK::CheckBox* LaneClearE = nullptr;
		inline MenuSDK::Slider* LaneMinionsAmount = nullptr;
		inline MenuSDK::MultiList* JungleClearE = nullptr;

		inline MenuSDK::CheckBox* ComboR = nullptr;
	}

	void Initialize();
	void Destroy();
}
