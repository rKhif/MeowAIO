#include "RivenQ.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::Q
		{
			bool IsReady( float delta )
			{
				const auto Player = Riot::ObjectManager::GetPlayer();

				if ( ( Player->GetSpellState( Riot::Spell::SpellSlot_Q ) & Riot::Spell::SpellState_NotLearned ) == Riot::Spell::SpellState_NotLearned )
					return false;

				return Player->GetSpell( Riot::Spell::SpellSlot_Q )->CooldownEndTime() <= Riot::GlobalClocks::LastUpdateNetworkTime() + delta || Player->GetSpellState( Riot::Spell::SpellSlot_Q ) == Riot::Spell::SpellState_Ready;
			}

			float Radius()
			{
				const auto Player = Riot::ObjectManager::GetPlayer();
				return radius_t[ Player->GetSpell( Riot::Spell::SpellSlot_Q )->Ammo() ];
			}

			float Delay()
			{
				const auto Player = Riot::ObjectManager::GetPlayer();
				return delay_t[ Player->GetSpell( Riot::Spell::SpellSlot_Q )->Ammo() ];
			}

			float Damage()
			{
				return 100.f;
			}

			Riot::Spell::SpellDataInst* GetSpell()
			{
				return Riot::ObjectManager::GetPlayer()->GetSpell( Riot::Spell::SpellSlot_Q );
			}
		}
	}
}
