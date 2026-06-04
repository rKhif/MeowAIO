#include "RivenR2.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::R2
		{
			bool IsReady( float delta )
			{
				return IsActive && Riot::ObjectManager::GetPlayer()->GetSpellState( Riot::Spell::SpellSlot_R ) == Riot::Spell::SpellState_Ready;
			}

			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo )
			{
				if ( CastInfo->GetNameHash() == SpellHash )
				{
					IsActive = false;
					LastCastTime = Riot::GlobalClocks::LastUpdateNetworkTime();
				}
			}

			void OnBuffGain( Riot::Spell::Buff::BuffInstance* Buff )
			{
				if ( Buff->Hash() == BuffHash )
				{
					IsActive = true;
					LastCastTime = Riot::GlobalClocks::LastUpdateNetworkTime();
				}
			}

			void OnBuffLose( Riot::Spell::Buff::BuffInstance* Buff )
			{
				if ( Buff->Hash() == BuffHash )
				{
					IsActive = false;
				}
			}

			Riot::Spell::SpellDataInst* GetSpell()
			{
				return Riot::ObjectManager::GetPlayer()->GetSpell( Riot::Spell::SpellSlot_R );
			}

			float Damage( Riot::AIBaseClient* Object )
			{
				const auto MissingHealthRatio = 1 + std::fminf( ( ( ( Object->GetMaxHealth() - Object->GetHealth() ) / Object->GetMaxHealth() ) * 100.f ) * 0.0267f, 2.f );
				return ( 50.f * GetSpell()->Level() + 0.6f * Riot::ObjectManager::GetPlayer()->TotalBonusAttackDamage() ) * MissingHealthRatio;
			}
		}
	}
}
