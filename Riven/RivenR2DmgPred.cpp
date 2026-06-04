#include "BaseSpell.h"
#include "DamageLib.h"
#include "RivenR1Pred.h"

#include "OrbwalkerSDK.h"
#include "RivenE.h"
#include "RivenR2.h"
#include "RivenW.h"
#include "RivenQ.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::R2Dmg
		{
			struct R2Result
			{
				Riot::AIBaseClient* Object;
				Riot::Vector3f CastPosition, SourcePosition;
			};

			Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;
			std::optional<R2Result> Result = std::nullopt;

			bool ConsiderKillable( Riot::AIBaseClient* Object, float Distance )
			{
				float Raw = Riot::ObjectManager::GetPlayer()->TotalAttackDamage();
				if ( Spell::W::IsReady( 0.f ) && Distance < Spell::W::Radius() )
				{
					Raw += Spell::W::Damage();
				}
				if ( Spell::Q::IsReady( 0.f ) && Distance < Spell::Q::Radius() )
				{
					Raw += Spell::Q::Damage();
				}
				Raw += Spell::R2::Damage( Object );
				const float PostMitigation = Damage::CalculatePhysicalDamage( Riot::ObjectManager::GetPlayer(), Object, Raw );
				return PostMitigation > Object->GetHealth();
			}

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
				const float Distance = Unit->Position().Distance( Riot::ObjectManager::GetPlayer()->Position() );
				SourcePosition = Riot::ObjectManager::GetPlayer()->Position();
				if ( Spell::E::IsReady( 0.f ) )
				{
					SourcePosition = Riot::ObjectManager::GetPlayer()->Position().Lerp( Unit->Position(), 225.f / Distance );
				}
				Prediction::PredictionResult PredRes;
				if ( PredictionSuccess( Prediction::GetPrediction( Unit, Input, SourcePosition, SourcePosition, true, &PredRes ) ) )
				{
					if ( ConsiderKillable( Unit, Distance ) )
					{
						Result = R2Result{ Unit, PredRes.CastPosition, SourcePosition };
						return true;
					}
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

			bool GetActionState()
			{
				if ( !GetSpellState() )
					return false;

				if ( GetPrediction() )
				{
					return Result.has_value();
				}

				return false;
			}

			void InvokeAction( bool Pause )
			{
				if ( Spell::E::IsReady( 0.f ) )
				{
					Riot::ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_E, Result.value().SourcePosition );
				}
				Riot::ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_R, Result.value().CastPosition );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}
		}
	}
}
