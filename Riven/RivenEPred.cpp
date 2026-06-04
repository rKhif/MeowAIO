#include "RivenEPred.h"

#include "BaseSpell.h"
#include "ChampionShared.h"
#include "NavigationMesh.h"
#include "OrbwalkerSDK.h"
#include "TargetSelector.h"
#include "RivenE.h"
#include "RivenPush.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::E //TODO refactor into less hanbot
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

				const float RealRadius = Radius + ObjectManager::GetPlayer()->GetBoundingRadius() + Unit->GetBoundingRadius();
				if ( DashPosition.DistanceSquared( Data.Position ) < RealRadius * RealRadius )
				{
					Result = { Unit, Data.Position };
					return true;
				}

				return false;
			}

			Riot::AIBaseClient* GetPrediction()
			{
				Result = std::nullopt;
				Radius = ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackRange.Value - 80.f;
				return TargetSelector::GetInstance()->GetBestTarget( TargetFilter );
			}

			Riot::AIBaseClient* GetActionState()
			{
				if ( !GetSpellState() )
					return nullptr;

				SourcePosition = ObjectManager::GetPlayer()->Position();
				return GetPrediction();
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
				return Spell::E::Radius() + ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackRange.Value - 80.f;
			}

			float GetDisplayRadius()
			{
				return GetTotalRadius() + 50.f;
			}

			bool GetSpellState()
			{
				return Spell::E::IsReady( 0.f );
			}

			float GetTotalDelay()
			{
				return Spell::E::Delay();
			}

			Riot::AIBaseClient* GetPushState()
			{
				if ( !GetSpellState() )
					return nullptr;

				Result = { ObjectManager::GetPlayer(), HudManager::GetInstance()->CursorTargetLogic()->CursorPosition() };

				return nullptr;
			}
		}
	}
}
