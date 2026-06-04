#include "RivenW.h"
#include "RivenR1.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::W
		{
			bool IsReady( float delta )
			{
				const auto Player = Riot::ObjectManager::GetPlayer();

				if ( ( Player->GetSpellState( Riot::Spell::SpellSlot_W ) & Riot::Spell::SpellState_NotLearned ) == Riot::Spell::SpellState_NotLearned )
					return false;

				return Player->GetSpell( Riot::Spell::SpellSlot_W )->CooldownEndTime() <= Riot::GlobalClocks::LastUpdateNetworkTime() + delta || Player->GetSpellState( Riot::Spell::SpellSlot_W ) == Riot::Spell::SpellState_Ready;
			}

			float Radius()
			{
				return R1::IsActive ? 375.f : 300.f;
			}

			float Damage()
			{
				return 35.f + GetSpell()->Level() * 30.f + Riot::ObjectManager::GetPlayer()->TotalBonusAttackDamage();
			}

			Riot::Spell::SpellDataInst* GetSpell()
			{
				return Riot::ObjectManager::GetPlayer()->GetSpell( Riot::Spell::SpellSlot_W );
			}
		}
	}
}
