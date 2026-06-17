#include "Jinx.h"
#include "../OrbwalkerSDK.h"
#include "../BuffCache.h"

namespace Features::Champions
{
	namespace Jinx::FlameChompers
	{
		static std::unordered_set AutoBuffHashes =
		{
			HASH( "ChronoRevive" ),
			HASH( "BardRStasis" ),
			HASH( "ZhonyasRingShield" ),
			HASH( "LissandraRSelf" )
		};

		std::unordered_map<uint32_t, std::pair<float, uint32_t>> AutoCache;

		bool IsCastAllowed()
		{
			return GlobalClocks::LastUpdateNetworkTime() - LastCast > .3f;
		}

		void OnBuffGainLose( AIBaseClient* Sender, BuffInstance* Instance, bool IsGain )
		{
			if ( !Sender->IsHero() )
				return;

			if ( IsGain )
			{
				if ( AutoBuffHashes.contains( Instance->Hash() ) )
				{
					AutoCache[ Sender->NetworkID() ] = { Instance->ExpireTime(), Instance->Hash() };
				}
			}
			else
			{
				if ( Instance->Hash() == HASH( "willrevive" ) )
				{
					AutoCache[ Sender->NetworkID() ] = { GlobalClocks::LastUpdateNetworkTime() + 4.f, 0x1337 };
				}
			}
		}

		bool HasShortPath( AIBaseClient* Unit )
		{
			if ( !Unit || !Unit->IsActive() )
				return false;

			if ( const auto& PathController = Unit->GetPathController() )
			{
				if ( !PathController->IsMoving() )
					return false;

				const auto NavPath = PathController->GetNavigationPath();
				return NavPath->Length() < 150.f;
			}

			return false;
		}

		bool IsDashing( AIBaseClient* Unit, Vector3f* EndPosition, float* TimeToEnd )
		{
			if ( !Unit || !Unit->IsActive() )
				return false;

			PathControllerCommon* const Controller = Unit->GetPathController();

			if ( !Controller || !Controller->HasNavigationPath() )
				return false;

			const NavigationPath* NavigationPath = Controller->GetNavigationPath();

			if ( Controller->IsDashing() && NavigationPath->IsDashing() && NavigationPath->StartPosition.Distance2D( NavigationPath->EndPosition ) >= 300.f )
			{
				*EndPosition = NavigationPath->EndPosition;
				*TimeToEnd = NavigationPath->StartPosition.Distance2D( NavigationPath->EndPosition ) / NavigationPath->DashSpeed;
				return true;
			}

			return false;
		}

		bool Auto()
		{
			if ( !Input.IsReady() || !IsCastAllowed() )
				return false;

			const auto SourcePosition = ObjectManager::GetPlayer()->Position();

			if ( AutoStasis->Value )
			{
				for ( const auto& Target : ManagerTemplates::Heroes() )
				{
					if ( Target->IsAlly() || !Target->IsVisible() || Target->IsTargetable() || SourcePosition.Distance2D( Target->Position() ) > Input.Range || !AutoCache.contains( Target->NetworkID() ) )
						continue;

					const float EndTime = AutoCache[ Target->NetworkID() ].first;
					if ( GlobalClocks::LastUpdateNetworkTime() < EndTime - .8f && Input.CastSpell( Target->Position() ) )
					{
						LastCastReason = std::format( ( "[2] {:.1f} / S 0x{:X} B 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), AutoCache[ Target->NetworkID() ].second );
						LastCast = GlobalClocks::LastUpdateNetworkTime();
						return true;
					}
				}
			}

			if ( AutoGapClose->Value )
			{
				for ( const auto& Target : TS->GetOrderedTargets() )
				{
					Vector3f DashEndPosition = Vector3f::Zero;
					float TimeToEnd = 0.f;

					if ( IsDashing( Target, &DashEndPosition, &TimeToEnd ) )
					{
						if ( SourcePosition.Distance2D( DashEndPosition ) < Input.Range )
						{
							const float TargetMS = Target->CharacterIntermediate()->MoveSpeed();
							const float TimeToExitE = Input.Radius / TargetMS;

							if ( TimeToEnd < .9f - TimeToExitE )
							{
								LastCastReason = std::format( ( "[2] {:.1f} / CL0 0x{:X} TMS {:.1f} TTEE {:.2f}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), TargetMS, TimeToExitE );
								LastCast = GlobalClocks::LastUpdateNetworkTime();
								return Input.CastSpell( DashEndPosition );
							}

							const float NecessaryChaseTime = .9f - TimeToEnd;
							const Vector3f BeingChasedPosition = DashEndPosition.Extend( SourcePosition, NecessaryChaseTime * TargetMS );
							const bool IsGapClose = SourcePosition.Distance2D( DashEndPosition ) < SourcePosition.Distance2D( Target->Position() );

							if ( MovementTracking::IsPlayerWalkingAwayFrom( Target, 0.5f ) && IsGapClose && SourcePosition.Distance2D( BeingChasedPosition ) < ObjectManager::GetPlayer()->CharacterIntermediate()->MoveSpeed() * 1.f && Input.CastSpell( BeingChasedPosition ) )
							{
								LastCastReason = std::format( ( "[2] {:.1f} / CL1 0x{:X} NCT {:.2f} TMS {:.1f} TTEE {:.2f}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), NecessaryChaseTime, TargetMS, TimeToExitE );
								LastCast = GlobalClocks::LastUpdateNetworkTime();
								return true;
							}
						}
					}
				}
			}

			for ( const auto& [ Target, Prediction ] : TS->GetBestTargets( Input, SourcePosition, SourcePosition ) )
			{
				switch ( static_cast<int>( Prediction.Hitchance ) )
				{
				case Prediction::CrowdControlled:
					{
						const float RemainingCCTime = BuffCache::GetCrowdControlTimeRemaining( Target );
						if ( RemainingCCTime - Prediction.TimeToTarget > std::fmaxf( MultiplayerClient::GetInstance()->CurrentRoundTripTime(), 0.033f * 2.5f ) )
							continue;

						if ( AutoCC->Value && Input.CastSpell( Prediction.CastPosition ) )
						{
							LastCastReason = std::format( ( "[2] {:.1f} / C0 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ) );
							LastCast = GlobalClocks::LastUpdateNetworkTime();
							return true;
						}
					}
					break;
				case Prediction::DashingMidAir:
				case Prediction::DashingEnd:
					{
						if ( AutoGapClose->Value && Input.CastSpell( Prediction.CastPosition ) )
						{
							LastCastReason = std::format( ( "[2] {:.1f} / CH 0x{:X} %s" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), static_cast<int>( Prediction.Hitchance ) == Prediction::DashingMidAir ? ( "DMA" ) : ( "DE" ) );
							LastCast = GlobalClocks::LastUpdateNetworkTime();
							return true;
						}
					}
					break;
				case Prediction::Undodgeable:
					{
						if ( AutoUndodgeable->Value && Input.CastSpell( Prediction.CastPosition ) )
						{
							LastCastReason = std::format( ( "[2] {:.1f} / U 0x{:X}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ) );
							LastCast = GlobalClocks::LastUpdateNetworkTime();
							return true;
						}
					}
					break;
				default:
					break;
				}
			}

			return false;
		}

		bool Execute()
		{
			if ( !Input.IsReady() || !IsCastAllowed() )
				return false;

			const auto SourcePosition = ObjectManager::GetPlayer()->Position();

			auto [ Target, Pred ] = TS->GetBestTarget( Input, SourcePosition, SourcePosition );

			if ( !Target )
				return false;

			if ( HasShortPath( Target ) || static_cast<int>( Pred.Hitchance ) < Prediction::Medium )
				return false;

			if ( SourcePosition.DistanceSquared2D( Pred.CastPosition ) > Input.RangeSquared * 0.98f )
				return false;

			const float DistanceSqrToPlayer = SourcePosition.DistanceSquared2D( Target->Position() );
			if ( DistanceSqrToPlayer <= 150.f * 150.f && Input.CastSpell( Pred.CastPosition ) )
			{
				LastCastReason = std::format( ( "[2] {:.1f} / CLO 0x{:X} {}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), static_cast<int>( Pred.Hitchance ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			if ( !Target->IsMoving() )
				return false;

			if ( MovementTracking::IsPlayerWalkingAwayFrom( Target, 0.5f ) && MovementTracking::IsWalkingTowardsPlayer( Target, 0.5f ) && DistanceSqrToPlayer <= 800.f * 800.f && Input.CastSpell( Pred.CastPosition ) )
			{
				LastCastReason = std::format( ( "[2] {:.1f} / EC 0x{:X} {}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), static_cast<int>( Pred.Hitchance ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			if ( MovementTracking::IsPlayerWalkingTowards( Target, 0.7f ) && DistanceSqrToPlayer <= 600.f * 600.f && Input.CastSpell( Pred.CastPosition ) )
			{
				LastCastReason = std::format( ( "[2] {:.1f} / CLL 0x{:X} {}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), static_cast<int>( Pred.Hitchance ) );
				LastCast = GlobalClocks::LastUpdateNetworkTime();
				return true;
			}

			if ( DistanceSqrToPlayer <= 430.f && Input.CastSpell( Pred.CastPosition ) )
			{
				LastCastReason = std::format( ( "[2] {:.1f} / CLS 0x{:X} {}" ), GlobalClocks::LastUpdateNetworkTime(), static_cast<unsigned int>( Target->SkinHash() ), static_cast<int>( Pred.Hitchance ) );
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

		float GetDamage( Riot::AIBaseClient* Unit )
		{
			return 0.f;
		}
	}
}
