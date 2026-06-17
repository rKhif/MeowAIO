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
#include "Evade.h"
#include "HealthPrediction.h"

#include <algorithm>

namespace Features::Champions::Zeri
{
	inline BaseSpell Q, W, WBeam, E, R;

	namespace Menu
	{
		// Combo
		inline MenuSDK::CheckBox* ComboQ;
		inline MenuSDK::CheckBox* ComboQIgnoreCollision;
		inline MenuSDK::CheckBox* ChainLogic;
		inline MenuSDK::CheckBox* ChainLogicOnlyKeepAlive;
		inline MenuSDK::MultiList* ComboAA; // Laser or Non-Charged
		inline MenuSDK::MultiList* ComboW;
		inline MenuSDK::CheckBox* OnlyOutOfRangeW;
		inline MenuSDK::MultiList* ComboE;
		inline MenuSDK::List* EMode;
		inline MenuSDK::CheckBox* OnlyReset;
		inline MenuSDK::CheckBox* CollisionIgnoreReset;
		inline MenuSDK::Slider* EnemyLimitE;
		inline MenuSDK::MultiList* ComboR;
		inline MenuSDK::Slider* RCount;

		// Harass/Hybrid
		inline MenuSDK::CheckBox* HarassQ;
		inline MenuSDK::MultiList* HarassAA; // Laser or Non-Charged
		inline MenuSDK::MultiList* HarassW;
		inline MenuSDK::CheckBox* HarassPrioritizeLastHit;

		// Lasthit
		inline MenuSDK::CheckBox* LasthitQ;
		inline MenuSDK::CheckBox* LasthitLaser;
		inline MenuSDK::CheckBox* LasthitAA;

		// Laneclear
		inline MenuSDK::List* LaneAA; // Always or Execute
		inline MenuSDK::CheckBox* LaneLaser;
		inline MenuSDK::CheckBox* LaneQ;
		inline MenuSDK::CheckBox* LaneQPlant;

		// Jungleclear
		inline MenuSDK::List* JungleAA; // Always or Execute
		inline MenuSDK::CheckBox* JungleLaser;
		inline MenuSDK::CheckBox* JungleQ;

		// Misc
		inline MenuSDK::CheckBox* KillstealW;
		inline MenuSDK::KeyBind* SemiAutoW;
		inline MenuSDK::CheckBox* KeepRAlive;
		inline MenuSDK::CheckBox* BlockR;
		inline MenuSDK::KeyBind* JumpE;
		inline MenuSDK::CheckBox* SaveManaE;

		// Draw
		inline MenuSDK::CheckBox* DrawQ;
		inline MenuSDK::CheckBox* DrawW;
		inline MenuSDK::CheckBox* DrawE;
		inline MenuSDK::CheckBox* DrawR;
		inline MenuSDK::CheckBox* DrawChainLightning;
		inline MenuSDK::CheckBox* DrawWallJumps, *DrawWallJumpsOnlyClose;
		inline MenuSDK::CheckBox* DrawWallEndPos;
		inline MenuSDK::ColorPicker* QColor;

		// Hitchance
		inline MenuSDK::List* HitchanceW = nullptr;
		inline MenuSDK::List* HitchanceBeam = nullptr;
	}

	void Initialize();
	void Destroy();

	constexpr uint32_t LaserReadyBuff = HASH( "ZeriQPassiveReady" );
	constexpr uint32_t EBuff = HASH( "ZeriESpecialRounds" );
	constexpr uint32_t RActiveBuff = HASH( "ZeriR" );
	constexpr uint32_t RSpeedBuff = HASH( "ZeriRStacks" );
	constexpr uint32_t GhostBuff = HASH( "SummonerHaste" );

	inline TargetSelector::Instance* TS;

	inline float LastCastTime[ 4 ] = { 0.f, 0.f, 0.f, 0.f };

	constexpr float QBase[ 7 ] = { 0.f, 15.f, 17.f, 19.f, 21.f, 23.f, 25.f };
	constexpr float QRatio[ 7 ] = { 0.f, 1.04f, 1.08f, 1.12f, 1.16f, 1.2f, 1.24f };

	constexpr float WBase[ 7 ] = { 0.f, 30.f, 70.f, 110.f, 150.f, 190.f, 230.f };
	constexpr float WADRatio = 1.3f;
	constexpr float WAPRatio = .25f;
	constexpr float CostW[ 7 ] = { 0.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f };

	constexpr float EBase[ 7 ] = { 18.f, 20.f, 22.f, 24.f, 26.f, 28.f, 30.f };
	constexpr float EBADRatio = 0.12f;
	constexpr float EAPRatio = 0.2f;
	constexpr float ECritRatio = 0.85f;
	constexpr float CostE[ 7 ] = { 0.f, 90.f, 85.f, 80.f, 75.f, 70.f, 65.f };

	constexpr float RBase[ 7 ] = { 75.f, 175.f, 275.f, 375.f, 475.f, 575.f, 675.f };
	constexpr float RBADRatio = 0.85f;
	constexpr float RAPRatio = 1.1f;
	constexpr float CostR = 100.f;

	const std::vector TrackedBuffHashList = { EBuff, RActiveBuff, RSpeedBuff, GhostBuff };
}
