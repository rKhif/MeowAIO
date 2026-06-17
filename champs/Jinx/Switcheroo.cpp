#include "Jinx.h"
#include "../OrbwalkerSDK.h"
#include "../Orbwalker.h"
#include "../ChampionShared.h"
#include "../DamageLib.h"
#include "../BuffCache.h"
#include "../OrbwalkerCore.h"
#include "../ComboMode.h"

namespace Features::Champions
{
	namespace Jinx::Switcheroo
	{
		float GetFishbonesBonusRange()
		{
			return 50.f + 30.f * static_cast<float>( Input.Level() );
		}

		bool QFilter( AIBaseClient* Unit )
		{
			const auto Range = ObjectManager::GetPlayer()->GetAutoAttackRange( nullptr ) + GetFishbonesBonusRange() + Unit->GetBoundingRadius();
			return Orbwalker::ComboMode::IsValidHeroTarget( Unit, Range );
		}

		void OnOutOfRange( AttackableUnit** pTarget )
		{
			if ( !Input.IsReady() && GlobalClocks::flGameTick() - LastQCast > 50.f )
				return;

			if ( ActiveWeapon == Weapon::Fishbones )
				return;

			if ( !Combo->Value || Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
				return;

			*pTarget = TS->GetBestTarget( QFilter );

			if ( *pTarget )
			{
				( void )Input.CastSpell();
				LastQCast = GlobalClocks::flGameTick();
			}
		}

		std::uint32_t GetQRampStacks()
		{
			if ( const auto& Buff = BuffCache::GetBuff( ObjectManager::GetPlayer(), AS_BUFF_HASH ) )
			{
				if ( !Buff->IsActive() )
					return 0;

				return Buff->GetStacks( 0 );
			}

			return 0;
		}

		float GetBonusAttackSpeedPerStack()
		{
			return 0.025f + 0.125f * static_cast<float>( Input.Level() );
		}

		float GetCurrentBonusAttackSpeed()
		{
			const auto BaseValue = GetBonusAttackSpeedPerStack();
			constexpr float EffectivenessPerStack[ 4 ] = { 0.f, 1.f, 1.5f, 2.f };

			return BaseValue * EffectivenessPerStack[ GetQRampStacks() ];
		}

		float GetPowPowAttackTime( float AttackCount )
		{
			const auto Player = ObjectManager::GetPlayer();
			if ( GetQRampStacks() == 3 )
			{
				const float BonusAttackSpeed = Player->CharacterIntermediate()->mAttackSpeedMod.Value + GetCurrentBonusAttackSpeed();
				const float AttackSpeed = 0.625f * BonusAttackSpeed;
				return AttackCount / AttackSpeed;
			}

			constexpr float EffectivenessPerStack[ 4 ] = { 0.f, 1.f, 1.5f, 2.f };
			const float BaseAttackSpeedPerStack = GetBonusAttackSpeedPerStack();
			const float BonusAttackSpeed = Player->CharacterIntermediate()->mAttackSpeedMod.Value;
			float TotalTime = 0.f;
			for ( int i = 0; i < static_cast<int>( AttackCount ); i++ )
			{
				const auto Effectiveness = EffectivenessPerStack[ min( 3, GetQRampStacks() + i ) ];
				TotalTime += 1.f / ( 0.625f * ( BonusAttackSpeed + BaseAttackSpeedPerStack * Effectiveness ) );
			}
			return TotalTime;
		}

		void OnBeforeAttack( AIBaseClient* Target )
		{
			if ( !Input.IsReady() && GlobalClocks::flGameTick() - LastQCast > 50.f )
				return;

			const auto Player = ObjectManager::GetPlayer();

			// Block swapping while mid swap
			//
			if ( !BuffCache::HasBuff( Player, HASH( "jinxqicon" ) ) && !BuffCache::HasBuff( Player, HASH( "JinxQ" ) ) )
				return;

			const auto IsFishbonesActive = ActiveWeapon == Weapon::Fishbones;

			if ( IsFishbonesActive && !Orbwalker::IsUnitInRange( Target, Player->GetAutoAttackRange( Target ) + Target->GetBoundingRadius() - GetFishbonesBonusRange() ) )
			{
				return;
			}

			if ( !Target->IsHero() )
			{
				if ( IsFishbonesActive && FarmMinigun->Value )
				{
					( void )Input.CastSpell();
					LastQCast = GlobalClocks::flGameTick();
				}
				return;
			}

			if ( AOE->Value )
			{
				const auto HeroesInRange = AllyHeroTargetsInRange( Target, ROCKET_AOE_RADIUS_SQR, 0.f, Target );

				if ( HeroesInRange > 0 )
				{
					if ( !IsFishbonesActive )
					{
						( void )Input.CastSpell();
						LastQCast = GlobalClocks::flGameTick();
					}
					return;
				}
			}

			if ( !Combo->Value )
				return;

			const auto FishBonesBonusDamage = GetDamage( Target );
			const auto AttackDamage = Damage::CalculateAutoAttackDamage( Player, Target );

			const auto AttacksNeeded = std::ceilf( Target->GetRealHealth( true ) / AttackDamage );
			const auto SwitchDamage = IsFishbonesActive ? AttackDamage - FishBonesBonusDamage : AttackDamage + FishBonesBonusDamage;
			const auto AttacksNeededSwitch = std::ceilf( Target->GetRealHealth( true ) / SwitchDamage );

			const float AttackSpeedFishbones = 0.625f * ( ( Player->CharacterIntermediate()->mAttackSpeedMod.Value - GetCurrentBonusAttackSpeed() ) * 0.9f );

			if ( IsFishbonesActive )
			{
				if ( GetPowPowAttackTime( AttacksNeededSwitch ) < AttacksNeeded / AttackSpeedFishbones )
				{
					( void )Input.CastSpell();
					LastQCast = GlobalClocks::flGameTick();
				}
			}
			else
			{
				if ( AttacksNeeded <= 1 )
					return;

				const auto PowPowAttackTime = GetPowPowAttackTime( AttacksNeeded );

				if ( PowPowAttackTime > AttacksNeededSwitch / AttackSpeedFishbones )
				{
					( void )Input.CastSpell();
					LastQCast = GlobalClocks::flGameTick();
				}
			}
		}

		bool Execute()
		{
			return false;
		}

		void Draw()
		{
			if ( !RangeDraw->Value )
				return;

			const auto Player = ObjectManager::GetPlayer();

			float Range;
			if ( ActiveWeapon == Weapon::Fishbones )
			{
				Range = Player->GetAutoAttackRange( nullptr ) - GetFishbonesBonusRange();
			}
			else
			{
				Range = Player->GetAutoAttackRange( nullptr ) + GetFishbonesBonusRange();
			}

			Graphics::AddCircle( Player->Position(), Range, 2, IM_COL32( 239, 131, 84, 150 ) );
		}

		float GetDamage( Riot::AIBaseClient* Unit )
		{
			const auto Player = ObjectManager::GetPlayer();
			const float RawDamage = 0.1f * Player->TotalAttackDamage();

			return Damage::CalculatePhysicalDamage( Player, Unit, RawDamage );
		}
	}
}
