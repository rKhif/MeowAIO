#include "RivenEFlashWPred.h"

#include "OrbwalkerSDK.h"
#include "RivenW.h"
#include "RivenE.h"
#include "RivenMenu.h"
#include "RivenFlash.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::EFlashW
		{
			Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;
			std::optional<EFlashWResult> Result = std::nullopt;
			float Radius = 0.f;
			float DashRadius = Spell::E::Radius() + Spell::Flash::Radius();
			float Delay = Spell::E::Delay();

			Riot::AIBaseClient* GetPrediction()
			{
				Radius = Spell::W::Radius() - 100.f;
				Result = std::nullopt;

				const auto SelectedTarget = TargetSelector::GetSelectedTarget();
				if ( !SelectedTarget )
					return nullptr;

				Prediction::PredictionData Data;
				Prediction::GetPositionAfterTime( SelectedTarget, Delay, &Data );

				const auto DashPosition = SourcePosition.Lerp( Data.Position, std::fminf( 1.f, DashRadius / SourcePosition.Distance( Data.Position ) ) );

				if ( DashPosition.DistanceSquared( Data.Position ) < Radius * Radius )
				{
					Result = EFlashWResult{ SelectedTarget, Data.Position };
					return SelectedTarget;
				}

				return nullptr;
			}

			bool GetSpellState()
			{
				return Spell::E::IsReady( 0.f ) && Spell::Flash::IsReady( 0.150f ) && Spell::W::IsReady( 0.150f );
			}

			std::optional<EFlashWResult> GetActionState()
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
				return Spell::W::Radius() + Spell::Flash::Radius() + Spell::E::Radius() - 100.f;
			}

			float GetTotalDelay()
			{
				return Spell::E::Delay();
			}
		}
	}
}
