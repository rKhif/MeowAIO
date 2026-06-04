#include "RivenR1.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::R1
		{
			bool IsReady( float delta )
			{
				if ( IsActive )
					return false;

				const auto Player = Riot::ObjectManager::GetPlayer();

				if ( ( Player->GetSpellState( Riot::Spell::SpellSlot_R ) & Riot::Spell::SpellState_NotLearned ) == Riot::Spell::SpellState_NotLearned )
					return false;

				return Player->GetSpell( Riot::Spell::SpellSlot_R )->CooldownEndTime() <= Riot::GlobalClocks::LastUpdateNetworkTime() + delta || Player->GetSpellState( Riot::Spell::SpellSlot_R ) == Riot::Spell::SpellState_Ready;
			}

			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo )
			{
				if ( CastInfo->GetNameHash() == NameHash )
				{
					IsActive = true;
					LastCastTime = Riot::GlobalClocks::LastUpdateNetworkTime();
				}
			}

			void OnBuffLose( Riot::Spell::Buff::BuffInstance* Buff )
			{
				if ( Buff->Hash() == NameHash )
				{
					IsActive = false;
				}
			}

			Riot::Spell::SpellDataInst* GetSpell()
			{
				return Riot::ObjectManager::GetPlayer()->GetSpell( Riot::Spell::SpellSlot_R );
			}
		}
	}
}
