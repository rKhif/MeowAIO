#include "RivenFlash.h"

#include "Flash.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::Flash
		{
			bool IsReady( float delta )
			{
				if (Activator::Flash::GetFlashSlot() == Riot::Spell::SpellSlot_Unknown)
					return false;

				const auto Player = Riot::ObjectManager::GetPlayer();
				return Player->GetSpell(Activator::Flash::GetFlashSlot())->CooldownEndTime() <= Riot::GlobalClocks::LastUpdateNetworkTime() + delta || Player->GetSpellState(Activator::Flash::GetFlashSlot()) == Riot::Spell::SpellState_Ready;
			}

			void OnCastSpell( Riot::Spell::kSpellSlot Slot, Riot::Vector3f StartPosition )
			{
				if (Activator::Flash::GetFlashSlot() == Riot::Spell::SpellSlot_Unknown || Activator::Flash::GetFlashSlot() != Slot )
					return;

				LastCastTime = Riot::GlobalClocks::LastUpdateNetworkTime();
				CastPosition = StartPosition;
			}

			float Radius()
			{
				return 400.f;
			}
		}
	}
}
