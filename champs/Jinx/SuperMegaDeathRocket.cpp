#include "Jinx.h"
#include "../OrbwalkerSDK.h"
#include "../ChampionShared.h"
#include "../DamageLib.h"

namespace Features::Champions
{
	namespace Jinx::SuperMegaDeathRocket
	{
		bool IsCastAllowed()
		{
			return GlobalClocks::LastUpdateNetworkTime() - LastCast > .3f;
		}

		bool Auto()
		{
			if ( !Kill->Value )
				return false;

			if ( !Input.IsReady() || !Orbwalker::SDK::CanAction() )
				return false;

			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

			auto PredictionFilter = [PlayerPosition] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* Result ) -> bool
			{
				if ( IsSionPassiveOrAniviaEgg( Target ) || !PredictionSuccess( Status ) )
				{
					return false;
				}

				const float DistanceToTarget = PlayerPosition.Distance2D( Target->Position() );
				const float DistanceToCast = PlayerPosition.Distance2D( Result->CastPosition );
				const float Damage = GetDamage( Target, DistanceToCast );

				// Killable Check
				//
				if ( Damage < Target->GetRealHealth( true, false ) )
				{
					return false;
				}

				// Dead before Arrival Check
				//
				if ( Damage::GetIncomingDamage( Target, Result->TimeToTarget ) >= Target->GetHealth() )
				{
					return false;
				}

				const auto ExtraRange = Switcheroo::ActiveWeapon == Switcheroo::Weapon::Fishbones ? 0.f : Switcheroo::GetFishbonesBonusRange();
				const auto Range = ObjectManager::GetPlayer()->GetAutoAttackRange( nullptr ) + ExtraRange + Target->GetBoundingRadius();

				// Don't ult in AA Range
				//
				if ( DistanceToTarget < Range * 1.1f )
				{
					return false;
				}

				// Menu Entry
				//
				if ( DistanceToCast > MaxRange->Value )
				{
					return false;
				}

				return true;
			};

			const auto& [ Target, Result ] = TS->GetBestTarget( Input, ObjectManager::GetPlayer()->Position(), nullptr, PredictionFilter );

			if ( Target && static_cast<int>( Result.Hitchance ) >= Prediction::Hitchance::High && Input.CastSpell( Result.CastPosition ) )
			{
				LastCastReason = std::format( ( "[3] {:.1f} / HC {} K 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<int>( Result.Hitchance ), static_cast<unsigned int>( Target->SkinHash() ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			return false;
		}

		bool SemiAuto()
		{
			if ( !Keypress->Value )
				return false;

			if ( !Input.IsReady() || !Orbwalker::SDK::CanAction() )
				return false;

			AIHeroClient* Player = ObjectManager::GetPlayer();
			const Vector3f LocalPosition = Player->Position();
			const Vector3f CursorPosition = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();
			const float CursorRadiusSquared = std::powf( static_cast<float>( CursorRadius->Value ), 2.0f );

			auto TargetFilter = [&] ( AIBaseClient* Target ) -> bool
			{
				if ( IsSionPassiveOrAniviaEgg( Target ) )
				{
					return false;
				}

				if ( UseCursorRadius->GetValue() )
				{
					const float DistanceFromCursor = CursorPosition.DistanceSquared2D( Target->Position() );
					if ( DistanceFromCursor > CursorRadiusSquared )
					{
						return false;
					}
				}

				if ( MaxRange->Value < Target->Position().Distance2D( LocalPosition ) )
				{
					return false;
				}

				return true;
			};

			const auto& [ Target, Result ] = TS->GetBestTarget( Input, LocalPosition, TargetFilter );

			if ( Target && static_cast<int>( Result.Hitchance ) >= Prediction::Hitchance::Medium && Input.CastSpell( Result.CastPosition ) )
			{
				LastCastReason = std::format( ( "[3] {:.1f} / HC {} K 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<int>( Result.Hitchance ), static_cast<unsigned int>( Target->SkinHash() ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			return false;
		}

		void Draw()
		{
			if ( !RangeDraw->Value )
				return;

			Graphics::AddCircle( ObjectManager::GetPlayer()->Position(), Input.Range, 2, IM_COL32( 255, 255, 255, 255 ) );
		}

		float GetDamage( Riot::AIBaseClient* Unit, float Distance )
		{
			constexpr float MaxDamage[ 7 ] = { 175.f, 325.f, 475.f, 625.f, 775.f, 925.f, 1075.f };
			constexpr float PercentDamage[ 7 ] = { 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.45f, 0.5f };
			constexpr float mCoefficient = 1.649999976158142f;

			const float RawMaxDamage = MaxDamage[ Input.Level() ] + mCoefficient * ObjectManager::GetPlayer()->TotalBonusAttackDamage();
			float DamageMod = 0.1f;
			float RemainingDistance = Distance;
			for ( int i = 1; i < 15; i++ )
			{
				RemainingDistance -= 100.f;

				if ( RemainingDistance <= 0.f )
				{
					break;
				}

				DamageMod += 0.06f;
			}

			DamageMod = std::fminf( DamageMod, 1.f );
			const float RealRawDamage = RawMaxDamage * DamageMod;
			const float RawPercentDamage = Unit->GetMissingHealth() * PercentDamage[ Input.Level() ];

			return Damage::CalculatePhysicalDamage( ObjectManager::GetPlayer(), Unit, RealRawDamage + RawPercentDamage );
		}
	}
}
