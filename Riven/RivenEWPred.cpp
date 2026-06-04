#include "RivenEWPred.h"

#include "BaseSpell.h"
#include "ChampionShared.h"
#include "NavigationMesh.h"
#include "OrbwalkerSDK.h"
#include "TargetSelector.h"
#include "RivenW.h"
#include "RivenE.h"
#include "RivenPush.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::EW //TODO refactor into less hanbot
		{
			float Delay = Spell::E::Delay();
			float DashRadius = Spell::E::Radius();
			float Radius = 0.f;

			std::optional<std::tuple<Riot::AIBaseClient*, Riot::Vector3f>> Result = std::nullopt;

			bool TargetFilter( Riot::AIBaseClient* Unit )
			{
				Prediction::PredictionData Data;
				Prediction::GetPositionAfterTime( Unit, Delay, &Data );

				const auto DashPosition = SourcePosition.Lerp( Data.Position, std::fminf( 1.f, DashRadius / SourcePosition.Distance( Data.Position ) ) );

				if ( !IsDashSafe( SourcePosition, DashPosition, 1200.f ) )
					return false;

				if ( NavMesh::IsWall( DashPosition ) )
					return false;

				if ( DashPosition.DistanceSquared( Data.Position ) < Radius * Radius )
				{
					Result = { Unit, SourcePosition.Lerp( Data.Position, Spell::E::Radius() / SourcePosition.Distance( Data.Position ) ) };
					return true;
				}

				return false;
			}

			Riot::AIBaseClient* GetPrediction()
			{
				Result = std::nullopt;

				Radius = Spell::W::Radius() - 80.f;
				return TargetSelector::GetInstance()->GetBestTarget( TargetFilter );
			}

			std::optional<std::tuple<Riot::AIBaseClient*, Riot::Vector3f>> GetActionState()
			{
				if ( !GetSpellState() )
					return std::nullopt;

				SourcePosition = ObjectManager::GetPlayer()->Position();
				GetPrediction();
				return Result;
			}

			void InvokeAction( bool Pause )
			{
				ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_E, std::get<1>( Result.value() ) );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}

			float GetTotalRadius()
			{
				return Spell::W::Radius() + Spell::E::Radius() - 80.f;
			}

			float GetDisplayRadius()
			{
				return GetTotalRadius() + 50.f;
			}

			bool GetSpellState()
			{
				return Spell::E::IsReady( 0 ) && Spell::W::IsReady( 0.150f );
			}

			float GetTotalDelay()
			{
				return Spell::E::Delay();
			}

			Riot::AIBaseClient* GetPushState()
			{
				Result = std::nullopt;

				if ( !GetSpellState() )
					return nullptr;

				auto [ Object, Position ] = Push::GetPrediction( GetTotalDelay(), GetTotalRadius(), false );
				if ( Object )
				{
					Result = { Object, ObjectManager::GetPlayer()->Position().Lerp( Position, Spell::E::Radius() / ObjectManager::GetPlayer()->Position().Distance( Position ) ) };
					return Object;
				}

				return nullptr;
			}
		}
	}
}
