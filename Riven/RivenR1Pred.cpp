#include "RivenR1Pred.h"

#include "OrbwalkerSDK.h"
#include "RivenR1.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::R1
		{
			bool GetSpellState()
			{
				return Spell::R1::IsReady( 0.f );
			}

			bool GetActionState()
			{
				return GetSpellState();
			}

			void InvokeAction( bool Pause )
			{
				Riot::ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_R );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}
		}
	}
}
