#include "RivenWPred.h"

#include "OrbwalkerSDK.h"
#include "TargetSelector.h"
#include "RivenW.h"
#include "RivenPush.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::W //TODO refactor into less hanbot
		{
			float Radius = 0.f;

			Riot::AIBaseClient* Target = nullptr;

			bool TargetFilter( Riot::AIBaseClient* Unit )
			{
				return SourcePosition.DistanceSquared( Unit->Position() ) < Radius * Radius;
			}

			Riot::AIBaseClient* GetPrediction()
			{
				Radius = Spell::W::Radius();
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
				ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_W );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}

			float GetTotalRadius()
			{
				return Spell::W::Radius() + ObjectManager::GetPlayer()->CharacterIntermediate()->mAttackRange.Value - 80.f;
			}

			float GetDisplayRadius()
			{
				return GetTotalRadius() + 50.f;
			}

			bool GetSpellState()
			{
				return Spell::W::IsReady( 0.f );
			}

			float GetTotalDelay()
			{
				return 0.f;
			}

			Riot::AIBaseClient* GetPushState()
			{
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
