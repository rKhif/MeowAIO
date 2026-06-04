#include "RivenFlashWPred.h"

#include "Flash.h"
#include "OrbwalkerSDK.h"
#include "RivenW.h"
#include "RivenMenu.h"
#include "RivenFlash.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::FlashW
		{
			Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;
			std::optional<FlashWResult> Result = std::nullopt;
			float Radius = 0.f;

			Riot::AIBaseClient* GetPrediction()
			{
				Radius = Spell::W::Radius() - 80.f;
				Result = std::nullopt;

				const auto SelectedTarget = TargetSelector::GetSelectedTarget();
				if ( !SelectedTarget )
					return nullptr;

				const auto DashPosition = SourcePosition.Lerp( SelectedTarget->Position(), std::fminf( 1.f, Spell::Flash::Radius() / SourcePosition.Distance( SelectedTarget->Position() ) ) );

				if ( DashPosition.DistanceSquared( SelectedTarget->Position() ) < Radius * Radius )
				{
					Result = FlashWResult{ SelectedTarget, DashPosition };
					return SelectedTarget;
				}

				return nullptr;
			}

			bool GetSpellState()
			{
				return Spell::Flash::IsReady( 0.f ) && Spell::W::IsReady( 0.f );
			}

			std::optional<FlashWResult> GetActionState( Riot::Vector3f Position )
			{
				if ( !GetSpellState() )
					return std::nullopt;

				SourcePosition = Position;

				if ( GetPrediction() )
				{
					return Result;
				}

				return std::nullopt;
			}

			void InvokeAction( bool Pause, bool FlashOnly )
			{
				Riot::ObjectManager::GetPlayer()->CastSpell(Activator::Flash::GetFlashSlot(), Result.value().Position );
				if ( !FlashOnly )
				{
					ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_W );
				}
				if ( Menu::ResetTS->Value )
				{
					if ( const auto Selected = TargetSelector::GetSelectedTarget() )
					{
						Selected->RemoveGlow();
					}
					TargetSelector::SelectedTarget = nullptr;
					TargetSelector::ForceSelectedTarget = nullptr;
				}
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}

			float GetTotalRadius()
			{
				return Spell::W::Radius() + Spell::Flash::Radius() - 80.f;
			}

			float GetTotalDelay()
			{
				return 0.f;
			}
		}
	}
}
