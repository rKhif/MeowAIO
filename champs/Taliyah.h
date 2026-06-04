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
#include "DelayedActions.h"

namespace Features::Champions::Taliyah {
	namespace Menu {
		inline MenuSDK::CheckBox* ComboQ;
		inline MenuSDK::List* MagnetQ;
		inline MenuSDK::MultiList* JungleQ;
		inline MenuSDK::CheckBox* DrawQ;
		inline MenuSDK::CheckBox* AOEQ;
		inline MenuSDK::ColorPicker* ColorQ;

		inline MenuSDK::CheckBox* ComboW;
		inline MenuSDK::KeyBind* SemiW;
		inline MenuSDK::MultiList* ModeW;
		inline MenuSDK::KeyBind* ToggleW;
		inline MenuSDK::MultiList* JungleW;
		inline MenuSDK::CheckBox* DrawW;
		inline MenuSDK::ColorPicker* ColorW;

		inline MenuSDK::CheckBox* ComboE;
		inline MenuSDK::CheckBox* AutoE;
		inline MenuSDK::CheckBox* GapE;
		inline MenuSDK::MultiList* JungleE;
		inline MenuSDK::CheckBox* DrawE;
		inline MenuSDK::ColorPicker* ColorE;

		inline MenuSDK::CheckBox* AutoRide;
		inline MenuSDK::CheckBox* DrawR;
	}

	enum Direction;

	namespace sE
	{
		float GetDamage(AIBaseClient* Client, Vector3f Override, Direction Direction);
	}

	inline BaseSpell *Q, Boulder, Volley, W, E, R;

	inline TargetSelector::Instance* TS;

	constexpr float CostW[6] = { 0.f, 40.f, 30.f, 20.f, 10.f, 0.f };
	constexpr float CostE[6] = { 0.f, 90.f, 95.f, 100.f, 105.f, 110.f };


	constexpr float BaseE[6] = { 0.f, 60.f, 105.f, 150.f, 195.f, 240.f };
	constexpr float RatioE = .6f;
	constexpr float MineE[6] = { 0.f, 25.f, 45.f, 65.f, 85.f, 105.f };
	constexpr float MineRatio = .3f;
	constexpr float Reduction[5] = { 0.f, 1.f, 1.75f, 2.25f, 2.5f };

	inline Shared::DelayedActions TaliyahActions;

	void Initialize();
	void Destroy();
}
