#include "RivenQPred.h"

#include "BaseSpell.h"
#include "ChampionShared.h"
#include "NavigationMesh.h"
#include "OrbwalkerSDK.h"
#include "TargetSelector.h"
#include "RivenQ.h"
#include "RivenPush.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::Q //TODO refactor into less hanbot
		{
			float Delay = 0.f;
			float DashRadius = 0.f;
			float Radius = 0.f;

			Riot::AIBaseClient* Target = nullptr;

			bool TargetFilter( Riot::AIBaseClient* Unit )
			{
				Prediction::PredictionData Data;
				Prediction::GetPositionAfterTime( Unit, Delay, &Data );

				const auto DashPosition = SourcePosition.Lerp( Data.Position, std::fminf( 1.f, DashRadius / SourcePosition.Distance( Data.Position ) ) );

				if ( !IsDashSafe( SourcePosition, DashPosition, FLT_MAX ) )
					return false;

				if ( NavMesh::IsWall( DashPosition ) )
					return false;

				const float RealRadius = Radius + ObjectManager::GetPlayer()->GetBoundingRadius() + Unit->GetBoundingRadius();
				return DashPosition.DistanceSquared( Data.Position ) < RealRadius * RealRadius;
			}

			Riot::AIBaseClient* GetPrediction()
			{
				Delay = Spell::Q::Delay();
				DashRadius = Spell::Q::Radius();
				Radius = ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackRange.Value - 80.f;
				Target = TargetSelector::GetInstance()->GetBestTarget( TargetFilter );
				return Target;
			}

			Riot::AIBaseClient* GetActionState( Riot::Vector3f Position )
			{
				if ( !GetSpellState() )
					return nullptr;

				SourcePosition = Position;
				return GetPrediction();
			}

			void InvokeAction( bool Pause )
			{
				ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, Target );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}

			float GetTotalRadius()
			{
				return Spell::Q::Radius() + ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackRange.Value - 80.f;
			}

			float GetDisplayRadius()
			{
				return GetTotalRadius() + 50.f;
			}

			bool GetSpellState()
			{
				return Spell::Q::IsReady( 0.f );
			}

			float GetTotalDelay()
			{
				return Spell::Q::Delay();
			}

			Riot::AIBaseClient* GetPushState()
			{
				Target = nullptr;

				if ( !GetSpellState() )
					return nullptr;

				auto [ Object, Position ] = Push::GetPrediction( GetTotalDelay(), GetTotalRadius(), false );
				if ( Object )
				{
					Target = Object;
					return Target;
				}

				return nullptr;
			}
		}
	}
}
