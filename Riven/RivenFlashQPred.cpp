#include "RivenFlashQPred.h"

#include "Flash.h"
#include "OrbwalkerSDK.h"
#include "RivenQ.h"
#include "RivenMenu.h"
#include "RivenFlash.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::FlashQ
		{
			struct FlashQResult
			{
				AIBaseClient* Object;
				Riot::Vector3f Position;
			};

			Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;
			std::optional<FlashQResult> Result = std::nullopt;
			float Radius = 0.f;
			float DashRadius = 0.f;
			float Delay = 0.f;

			Riot::AIBaseClient* GetPrediction()
			{
				Radius = ObjectManager::GetPlayer()->CharacterIntermediate()->AttackRange() - 80.f;
				Delay = Spell::Q::Delay();
				DashRadius = Spell::Q::Radius() + Spell::Flash::Radius();
				Result = std::nullopt;

				const auto SelectedTarget = TargetSelector::GetSelectedTarget();
				if ( !SelectedTarget )
					return nullptr;

				Prediction::PredictionData Data;
				Prediction::GetPositionAfterTime( SelectedTarget, Delay, &Data );

				const auto DashPosition = SourcePosition.Lerp( Data.Position, std::fminf( 1.f, DashRadius / SourcePosition.Distance( Data.Position ) ) );

				if ( DashPosition.DistanceSquared( Data.Position ) < Radius * Radius )
				{
					Result = FlashQResult{ SelectedTarget, Data.Position };
					return SelectedTarget;
				}

				return nullptr;
			}

			bool GetSpellState()
			{
				return Spell::Flash::IsReady( 0.f ) && Spell::Q::IsReady( 0.f );
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

			void InvokeAction( bool Pause, bool FlashOnly )
			{
				Riot::ObjectManager::GetPlayer()->CastSpell(Activator::Flash::GetFlashSlot(), Result.value().Position );
				if ( !FlashOnly )
				{
					ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, Result.value().Object );
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
				return Spell::Q::Radius() + Spell::Flash::Radius() + ObjectManager::GetPlayer()->CharacterIntermediate()->AttackRange() - 80.f;
			}

			float GetTotalDelay()
			{
				return Spell::Q::Delay();
			}
		}
	}
}
