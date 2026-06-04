#include "RivenPush.h"

#include "Farming.h"
#include "Prediction.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::Push
		{
			Riot::AIBaseClient* GetNearestMinionToMouse()
			{
				const auto CursorPos = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();
				float MinDist = FLT_MAX;
				Riot::AIBaseClient* BestObject = nullptr;

				for ( const auto& Minion : Riot::ManagerTemplates::Minions() )
				{
					if ( !Orbwalker::Farming::IsMinionTargetValid( Minion ) )
						continue;

					const float DistSqr = Minion->Position().DistanceSquared2D( CursorPos );
					if ( DistSqr < MinDist )
					{
						MinDist = DistSqr;
						BestObject = Minion;
					}
				}

				return BestObject;
			}

			std::tuple<Riot::AIBaseClient*, Riot::Vector3f> GetPrediction( float Delay, float Radius, bool UseBoundingRadius )
			{
				if ( LastUpdateTick < GlobalClocks::flGameTick() )
				{
					Object = GetNearestMinionToMouse();
					LastUpdateTick = GlobalClocks::flGameTick();
				}

				if ( Object )
				{
					Prediction::PredictionData Data;
					Prediction::GetPositionAfterTime( Object, Delay, &Data );
					const float ExtraRadius = UseBoundingRadius ? Object->GetBoundingRadius() : 0.f;
					const float TotalRadius = Radius + ExtraRadius;
					if ( Data.Position.DistanceSquared( ObjectManager::GetPlayer()->Position() ) < TotalRadius * TotalRadius )
					{
						return { Object, Data.Position };
					}
				}

				return { nullptr, Riot::Vector3f::Zero };
			}
		}
	}
}
