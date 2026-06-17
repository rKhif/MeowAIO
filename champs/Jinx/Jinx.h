#pragma once
#include "../BaseSpell.h"
#include "../Menu.h"
#include "../TargetSelector.h"
#include "../OrbwalkerDefines.h"
#include "optional"

namespace Features::Champions
{
	namespace Jinx
	{
		enum Direction
		{
			Towards,
			Away,
			Other
		};

		struct ChasingData
		{
			Direction Direction;
			float StartTime;
			float LastUpdateTime;
		};

		namespace MovementTracking
		{
			bool IsWalkingTowardsPlayer( AIBaseClient* Unit, float Time );
			bool IsWalkingAwayFromPlayer( AIBaseClient* Unit, float Time );

			bool IsPlayerWalkingTowards( AIBaseClient* Unit, float Time );
			bool IsPlayerWalkingAwayFrom( AIBaseClient* Unit, float Time );
		}

		namespace GetExcited
		{
			inline bool Active = false;
		}

		inline MenuSDK::CheckBox* DebugMode = nullptr;

		namespace Switcheroo
		{
			enum class Weapon
			{
				Fishbones,
				PowPow
			};

			constexpr std::uint32_t AS_BUFF_HASH = HASH( "jinxqramp" );
			constexpr float ROCKET_AOE_RADIUS_SQR = 250.f * 250.f;

			inline Weapon ActiveWeapon = Weapon::PowPow;

			inline MenuSDK::CheckBox* Combo = nullptr;
			inline MenuSDK::CheckBox* AOE = nullptr;
			inline MenuSDK::CheckBox* FarmMinigun = nullptr;
			inline MenuSDK::CheckBox* RangeDraw = nullptr;

			inline BaseSpell Input;
			inline float LastQCast = 0.f;
			inline std::string LastCastReason = xorstr_( "[0] None" );

			float GetFishbonesBonusRange();

			bool Execute();
			void Draw();
			void OnOutOfRange( AttackableUnit** pTarget );
			void OnBeforeAttack( AIBaseClient* Target );
			float GetDamage( AIBaseClient* Unit );
		}

		namespace Zap
		{
			inline MenuSDK::CheckBox* Combo = nullptr;
			inline MenuSDK::CheckBox* AutoCC = nullptr;
			inline MenuSDK::CheckBox* AutoKill = nullptr;
			inline MenuSDK::CheckBox* Harass = nullptr;
			inline MenuSDK::KeyBind* Semi = nullptr;
			inline MenuSDK::CheckBox* DontWInAARange = nullptr;
			inline MenuSDK::CheckBox* Weave = nullptr;
			inline MenuSDK::CheckBox* RangeDraw = nullptr;

			inline float LastCast = 0.f;
			inline BaseSpell Input;
			inline std::string LastCastReason = xorstr_( "[1] None" );

			std::optional<Prediction::PredictionResult> GetCastData( Prediction::Hitchance MinHitchance );
			bool Auto();
			bool Execute();
			void Draw();
			float GetDamage( AIBaseClient* Unit );
		}

		namespace FlameChompers
		{
			//TODO combo(always, some logic, on cc/slow/undodgeable), auto(anti gap, cc, channel, anti-gap, revive etc)
			inline MenuSDK::CheckBox* Combo = nullptr;
			inline MenuSDK::CheckBox* RangeDraw = nullptr;
			inline MenuSDK::CheckBox* AutoStasis = nullptr;
			inline MenuSDK::CheckBox* AutoCC = nullptr;
			inline MenuSDK::CheckBox* AutoGapClose = nullptr;
			inline MenuSDK::CheckBox* AutoUndodgeable = nullptr;

			inline float LastCast = 0.f;
			inline BaseSpell Input;
			inline std::string LastCastReason = xorstr_( "[2] None" );

			void OnBuffGainLose( AIBaseClient* Sender, Spell::Buff::BuffInstance* Instance, bool IsGain );
			bool Auto();
			bool Execute();
			void Draw();
			float GetDamage( AIBaseClient* Unit );
		}

		namespace SuperMegaDeathRocket
		{
			//TODO auto kill, block if enemy has blink/dash, good way to pred
			inline MenuSDK::CheckBox* Kill = nullptr;
			inline MenuSDK::KeyBind* Keypress = nullptr;
			inline MenuSDK::CheckBox* RangeDraw = nullptr;
			inline MenuSDK::CheckBox* UseCursorRadius = nullptr;
			inline MenuSDK::Slider* CursorRadius = nullptr;
			inline MenuSDK::SliderF* MaxRange = nullptr;

			inline float LastCast = 0.f;
			inline BaseSpell Input;
			inline std::string LastCastReason = xorstr_( "[3] None" );

			bool Auto();
			bool SemiAuto();
			void Draw();
			float GetDamage( AIBaseClient* Unit, float Distance );
		}

		inline TargetSelector::Instance* TS = nullptr;
		inline std::unordered_map<std::uint32_t, ChasingData> ChasingMap = {};
		inline std::unordered_map<std::uint32_t, ChasingData> PlayerMovementMap = {};

		void OnGameUpdate();
		void OnDrawWorld();
		void OnBuffGainLose( AIBaseClient* Sender, Spell::Buff::BuffInstance* Buff, bool IsGain );
		void OnOrbOutOfRange( Riot::AttackableUnit** Target );
		void OnOrbwalkerAction( Features::Orbwalker::ActionArgs* Args, bool* Process );
		void OnNewPath( AIBaseClient* Sender, std::vector<Vector3f>* Nodes, bool IsDash, float DashSpeed );

		void Initialize();
		void Destroy();
	}
}
