#include "RivenCrescent.h"

#include <set>

#include "OrbwalkerSDK.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Item::Crescent
		{
			Riot::Spell::kSpellSlot Slot = Riot::Spell::SpellSlot_Unknown;
			std::set<::Item> CrescentItems = { ::Item::RavenousHydra, ::Item::Stridebreaker, ::Item::Dreamshatter, ::Item::ProfaneHydra };

			bool GetSpellState()
			{
				Slot = Riot::Spell::SpellSlot_Unknown;

				auto inventorySlots = Riot::ObjectManager::GetPlayer()->Inventory()->Slots;
				for ( auto i = 0; i < 7; i++ )
				{
					auto item = inventorySlots[ i ];
					auto itemInfo = item->GetItemInfo();
					if ( itemInfo && CrescentItems.contains( itemInfo->GetItemData()->ItemID() ) )
					{
						Slot = static_cast<Riot::Spell::kSpellSlot>( i + 6 );
						break;
					}
				}

				return Riot::ObjectManager::GetPlayer()->GetSpellState( Slot ) == Riot::Spell::SpellState_Ready;
			}

			bool GetActionState()
			{
				if ( !GetSpellState() )
					return false;

				return TargetSelector::GetInstance()->GetBestTarget( 450.f ) != nullptr;
			}

			void InvokeAction( bool Pause )
			{
				ObjectManager::GetPlayer()->CastSpell( Slot );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}
		}
	}
}
