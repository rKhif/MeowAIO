#include "Jinx.h"
#include "../Orbwalker.h"
#include "../OrbwalkerSDK.h"
#include "../DamageLib.h"
#include "../CooldownTracker.h"
#include "../BuffCache.h"
#include "../ComboMode.h"

namespace Features::Champions
{
	namespace Jinx::Zap
	{
		void UpdateInput()
		{
			AIHeroClient* const Player = ObjectManager::GetPlayer();
			const float BonusAttackSpeedPercentage = ( Player->CharacterIntermediate()->AttackSpeedMod() - 1.0f );
			const float CalculatedDelay = 0.6f - 0.02f * min( int(BonusAttackSpeedPercentage / 0.25f), 10.f );
			Input.CastDelay = CalculatedDelay;
		}

		bool AARangeFilter( AIBaseClient* Unit )
		{
			const auto ExtraRange = Switcheroo::ActiveWeapon == Switcheroo::Weapon::Fishbones ? 0.f : Switcheroo::GetFishbonesBonusRange();
			const auto Range = ObjectManager::GetPlayer()->GetAutoAttackRange( nullptr ) + ExtraRange + Unit->GetBoundingRadius();
			return Orbwalker::ComboMode::IsValidHeroTarget( Unit, Range );
		}

		bool IsCastAllowed()
		{
			return GlobalClocks::LastUpdateNetworkTime() - LastCast > .3f;
		}

		bool HasNearbyCC()
		{
			const float GameTime = GlobalClocks::LastUpdateNetworkTime();
			const auto PlayerPosition = ObjectManager::GetPlayer()->Position();

			for ( AIBaseClient* Enemy : ManagerTemplates::Heroes() )
			{
				if ( !Enemy || !Enemy->IsEnemy() || Enemy->IsDead() || Enemy->mHealth()->ShouldBeDead() )
				{
					continue;
				}

				auto Vision = Enemy->GetVisionState();
				if ( !Vision.State )
				{
					if ( ( GameTime - Vision.Time ) > 3.0f )
					{
						continue;
					}
				}

				if ( Enemy->SkinHash() == SkinHash::Jax )
				{
					if ( BuffCache::HasBuff( Enemy, HASH( "jaxcounterstrike" ) ) )
					{
						return true;
					}
				}

				auto& Tracked = CooldownTracker::Champions[ Enemy->NetworkID() ];

				for ( const auto& [ Slot,Spell ] : Tracked.Spells )
				{
					if ( !Spell.IsHardCC )
						continue;

					// bandaid in case of fake 25000 castrange spell
					//
					const float SpellRange = std::fminf( Tracked.Hero->GetSpell( Spell.Slot )->CastRange() + 100.f, 2500.f );
					if ( PlayerPosition.Distance2D( Enemy->Position() ) > SpellRange )
					{
						continue;
					}

					if ( Spell.IsReady() || ( Enemy->GetSpell( Spell.Slot )->HasLearned() && ( Spell.EndTime - GameTime ) < 0.25f ) )
					{
						return true;
					}
				}
			}

			return false;
		}

		std::optional<Prediction::PredictionResult> GetCastData( Prediction::Hitchance MinHitchance )
		{
			UpdateInput();

			if ( !Input.IsReady() )
				return std::nullopt;

			if ( HasNearbyCC() )
				return std::nullopt;

			const auto Player = ObjectManager::GetPlayer();
			const auto PlayerPosition = Player->Position();

			AIBaseClient* Target = TS->GetBestTarget( AARangeFilter );

			if ( Target )
			{
				Prediction::PredictionResult Result;
				const auto Validity = TS->IsValidPredTarget( Target, &Result, Input, PlayerPosition, PlayerPosition, true );
				if ( !VALID_PREDICTION( Validity ) )
				{
					return std::nullopt;
				}

				if ( static_cast<int>( Result.Hitchance ) < MinHitchance )
				{
					return std::nullopt;
				}

				const float AutoAttackCD = Orbwalker::SDK::TimeUntilAttackReady();
				if ( AutoAttackCD >= 0.0f && AutoAttackCD < Input.CastDelay && Weave->Value )
				{
					const auto AutoAttackDamage = Damage::CalculateAutoAttackDamage( ObjectManager::GetPlayer(), Target ) + Switcheroo::GetDamage( Target );
					const auto WDamage = GetDamage( Target );
					const auto TargetHealth = Target->GetRealHealth( true ) - Damage::GetIncomingDamage( Target, Result.TimeToTarget );
					if ( TargetHealth > 0.f && WDamage > TargetHealth && WDamage > AutoAttackDamage * std::floorf( Result.TimeToTarget * Orbwalker::GetAttackSpeed( Player ) ) )
					{
						const auto ExtraRange = Switcheroo::ActiveWeapon == Switcheroo::Weapon::Fishbones ? 0.f : Switcheroo::GetFishbonesBonusRange();
						const auto AttackRange = ObjectManager::GetPlayer()->GetAutoAttackRange( nullptr ) + ExtraRange + Target->GetBoundingRadius();
						const auto MoveRange = Target->CharacterIntermediate()->MoveSpeed() * Result.TimeToTarget;
						const auto DistanceToPlayer = PlayerPosition.DistanceSquared( Target->Position() );
						if ( static_cast<int>( Result.Hitchance ) < Prediction::VeryHigh && DistanceToPlayer + MoveRange > AttackRange )
						{
							return std::nullopt;
						}

						return Result;
					}

					return std::nullopt;
				}

				if ( DontWInAARange->Value )
				{
					return std::nullopt;
				}

				return Result;
			}

			const auto& [ OutOfAARangeTarget, Result ] = TS->GetBestTarget( Input, PlayerPosition );

			if ( !OutOfAARangeTarget )
			{
				return std::nullopt;
			}

			if ( static_cast<int>( Result.Hitchance ) < MinHitchance )
			{
				return std::nullopt;
			}

			if ( Damage::GetIncomingDamage( OutOfAARangeTarget, Result.TimeToTarget ) >= OutOfAARangeTarget->GetRealHealth() )
			{
				return std::nullopt;
			}

			return Result;
		}

		bool Auto()
		{
			if ( Semi->Value )
			{
				const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
				auto [ Target, Prediction ] = TS->GetBestTarget( Input, PlayerPosition, PlayerPosition );

				if ( Target && static_cast<int>( Prediction.Hitchance ) >= Prediction::Medium && Input.CastSpell( Prediction.CastPosition ) )
				{
					LastCastReason = std::format( ( "[1] {:.1f} / HC {} S 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<int>( Prediction.Hitchance ), static_cast<unsigned int>( Target->SkinHash() ) );
					LastCast = GlobalClocks::LastUpdateNetworkTime();
					return true;
				}
			}

			const auto Result = GetCastData( Prediction::High );
			if ( !Result )
				return false;

			const auto Value = Result.value();
			if ( AutoCC->Value && static_cast<int>( Value.Hitchance ) >= Prediction::CrowdControlled && Input.CastSpell( Value.CastPosition ) )
			{
				LastCastReason = std::format( ( "[1] {:.1f} / HC {} C 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<int>( Value.Hitchance ), static_cast<unsigned int>( Value.Data.Object->SkinHash() ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			if ( !AutoKill->Value )
				return false;

			if ( const auto& Target = Value.Data.Object )
			{
				const auto WDamage = GetDamage( Target );
				const auto TargetHealth = Target->GetRealHealth( true );

				if ( WDamage > TargetHealth && Input.CastSpell( Value.CastPosition ) )
				{
					LastCastReason = std::format( ( "[1] {:.1f} / HC {} K 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<int>( Value.Hitchance ), static_cast<unsigned int>( Value.Data.Object->SkinHash() ) );
					LastCast = GlobalClocks::LastUpdateNetworkTime();
					return true;
				}
			}

			return false;
		}

		bool Execute()
		{
			const auto Result = GetCastData( Prediction::High );

			if ( !Result )
				return false;

			auto Value = Result.value();

			if ( Input.CastSpell( Value.CastPosition ) )
			{
				LastCastReason = std::format( ( "[1] {:.1f} / HC {} E 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<int>( Value.Hitchance ), static_cast<unsigned int>( Value.Data.Object->SkinHash() ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			return false;
		}

		void Draw()
		{
			if ( const auto Result = GetCastData( Prediction::High ) )
			{
				Graphics::AddCircle( Result.value().CastPosition, 25.f, 3, IM_COL32( 255, 0, 0, 255 ) );
			}

			if ( !RangeDraw->Value )
				return;

			Graphics::AddCircle( ObjectManager::GetPlayer()->Position(), Input.Range, 2, IM_COL32( 255, 255, 255, 255 ) );
		}

		float GetDamage( Riot::AIBaseClient* Unit )
		{
			constexpr float Damage[ 7 ] = { -40.f, 10.f, 60.f, 110.f, 160.f, 210.f, 260.f };
			constexpr float mCoefficient = 1.600000023841858f;

			const auto Player = ObjectManager::GetPlayer();
			const float RawDamage = Damage[ Input.Level() ] + mCoefficient * Player->TotalAttackDamage();

			return Damage::CalculatePhysicalDamage( Player, Unit, RawDamage );
		}
	}
}
