#include "RivenE.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::E
		{
			bool IsReady( float delta )
			{
				const auto Player = Riot::ObjectManager::GetPlayer();

				if ( ( Player->GetSpellState( Riot::Spell::SpellSlot_E ) & Riot::Spell::SpellState_NotLearned ) == Riot::Spell::SpellState_NotLearned )
					return false;

				return Player->GetSpell( Riot::Spell::SpellSlot_E )->CooldownEndTime() <= Riot::GlobalClocks::LastUpdateNetworkTime() + delta || Player->GetSpellState( Riot::Spell::SpellSlot_E ) == Riot::Spell::SpellState_Ready;
			}

			float Delay()
			{
				return 0.233f;
			}

			float Radius()
			{
				return 250.f;
			}

			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo )
			{
				if ( CastInfo->Slot() != Riot::Spell::SpellSlot_E )
					return;

				Position = CastInfo->StartPosition().Lerp( CastInfo->EndPosition(), 250.f / CastInfo->StartPosition().Distance( CastInfo->EndPosition() ) );
			}

			Riot::Spell::SpellDataInst* GetSpell()
			{
				return Riot::ObjectManager::GetPlayer()->GetSpell( Riot::Spell::SpellSlot_E );
			}
		}
	}
}
