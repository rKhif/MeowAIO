#include "BaseSpell.h"
#include "DamageLib.h"
#include "RivenR2Pred.h"

#include "OrbwalkerSDK.h"
#include "RivenR2.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::R2
		{
			struct R2Result
			{
				Riot::AIBaseClient* Object;
				Riot::Vector3f CastPosition, SourcePosition;
			};

			Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;
			std::optional<R2Result> Result = std::nullopt;

			BaseSpell Input = BaseSpell{
				BaseSpellData
				{
					.Slot = Riot::Spell::SpellSlot_R,
					.Type = Linear,
					.CastDelay = 0.25f,
					.Range = 1000.f,
					.RangeType = Edge,
					.Speed = 1600.f,
					.Radius = 25.f,
					.CollisionFlags = CollideWindWall | CollideBladeWhirl | CollideShield
				}
			};

			bool TargetFilter( Riot::AIBaseClient* Unit )
			{
				Prediction::PredictionResult PredRes;
				if ( PredictionSuccess( Prediction::GetPrediction(Unit, Input, SourcePosition, SourcePosition, true, &PredRes) ) )
				{
					Result = R2Result{ Unit, PredRes.CastPosition, SourcePosition };
					return true;
				}
				return false;
			}

			Riot::AIBaseClient* GetPrediction()
			{
				Result = std::nullopt;
				return TargetSelector::GetInstance()->GetBestTarget( TargetFilter );
			}

			bool GetSpellState()
			{
				return Spell::R2::IsReady( 0.f );
			}

			bool GetActionState( Riot::Vector3f Position )
			{
				if ( !GetSpellState() )
					return false;

				SourcePosition = Position;

				if ( GetPrediction() )
				{
					return Result.has_value();
				}

				return false;
			}

			void InvokeAction( bool Pause )
			{
				Riot::ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_R, Result.value().CastPosition );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}
		}
	}
}
