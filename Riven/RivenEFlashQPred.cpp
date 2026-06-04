#include "RivenEFlashQPred.h"

#include "OrbwalkerSDK.h"
#include "RivenQ.h"
#include "RivenE.h"
#include "RivenMenu.h"
#include "RivenFlash.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::EFlashQ
		{
			Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;
			std::optional<EFlashQResult> Result = std::nullopt;
			float Radius = 0.f;
			float DashRadius = 0.f;
			float Delay = 0.f;

			Riot::AIBaseClient* GetPrediction()
			{
				Delay = Spell::Q::Delay() + Spell::E::Delay();
				Radius = ObjectManager::GetPlayer()->CharacterIntermediate()->AttackRange() - 100.f;
				DashRadius = Spell::E::Radius() + Spell::Q::Radius() + Spell::Flash::Radius();
				Result = std::nullopt;

				const auto SelectedTarget = TargetSelector::GetSelectedTarget();
				if ( !SelectedTarget )
				{
					return nullptr;
				}

				Prediction::PredictionData Data;
				Prediction::GetPositionAfterTime( SelectedTarget, Delay, &Data );

				const auto DashPosition = SourcePosition.Lerp( Data.Position, std::fminf( 1.f, DashRadius / SourcePosition.Distance( Data.Position ) ) );

				if ( DashPosition.DistanceSquared( Data.Position ) < Radius * Radius )
				{
					Result = EFlashQResult{ SelectedTarget, SourcePosition.Lerp( Data.Position, 250.f / SourcePosition.Distance( Data.Position ) ) };
					return SelectedTarget;
				}

				return nullptr;
			}

			bool GetSpellState()
			{
				return Spell::E::IsReady( 0.f ) && Spell::Flash::IsReady( 0.150f ) && Spell::Q::IsReady( 0.150f );
			}

			std::optional<EFlashQResult> GetActionState()
			{
				if ( !GetSpellState() )
					return std::nullopt;

				SourcePosition = Riot::ObjectManager::GetPlayer()->Position();

				if ( GetPrediction() )
				{
					return Result;
				}

				return std::nullopt;
			}

			void InvokeAction( bool Pause )
			{
				ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_E, Result.value().Position );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}

			float GetTotalRadius()
			{
				return Spell::Q::Radius() + Spell::Flash::Radius() + Spell::E::Radius() + ObjectManager::GetPlayer()->CharacterIntermediate()->AttackRange() - 100.f;
			}

			float GetTotalDelay()
			{
				return Spell::E::Delay() + Spell::Q::Delay();
			}
		}
	}
}
