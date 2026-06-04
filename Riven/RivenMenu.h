#pragma once
#include "Menu.h"
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Menu
		{
			inline MenuSDK::CheckBox* PushW = nullptr;
			inline MenuSDK::CheckBox* PushE = nullptr;

			inline MenuSDK::KeyBind* Flash = nullptr;
			inline MenuSDK::CheckBox* ResetTS = nullptr;
			inline MenuSDK::CheckBox* FlashOnlyR = nullptr;
			inline MenuSDK::Slider* R2Flash = nullptr;

			inline MenuSDK::CheckBox* GapEW = nullptr;
			inline MenuSDK::CheckBox* GapEQ = nullptr;
			inline MenuSDK::CheckBox* GapEAA = nullptr;
			inline MenuSDK::CheckBox* GapQ = nullptr;

			inline MenuSDK::CheckBox* SCancel = nullptr;

			inline MenuSDK::Slider* EDoubleCastWeight = nullptr;
			inline MenuSDK::KeyBind* EAA = nullptr;
			inline MenuSDK::KeyBind* R1 = nullptr;

			inline void Initialize()
			{
				const auto mm = MenuSDK::Menu::CreateMenu( xorstr_( "gagong_riven" ), xorstr_( "Gagong Riven" ) );
				mm->AddTexture( Riot::ObjectManager::GetPlayer()->GetIconSquare() );

				const auto push = mm->AddMenu( xorstr_( "header_push" ), xorstr_( "Panic Clear" ) );
				{
					PushW = push->AddCheckBox( xorstr_( "push_w" ), xorstr_( "Use W" ), true );
					PushE = push->AddCheckBox( xorstr_( "push_e" ), xorstr_( "Use E" ), true );
				}

				const auto flash = mm->AddMenu( xorstr_( "header_flash" ), xorstr_( "Flash Combo" ) );
				{
					Flash = flash->AddKeyBind( xorstr_( "flash" ), xorstr_( "Use flash combo on selected target" ), 'U', false, true );
					ResetTS = flash->AddCheckBox( xorstr_( "reset_ts" ), xorstr_( "Reset selected target after flash" ), true );
					FlashOnlyR = flash->AddCheckBox( xorstr_( "flash_only_r" ), xorstr_( "Use flash only for r1/r2 combos" ), false );
					R2Flash = flash->AddSlider( xorstr_( "r2_flash" ), xorstr_( "Flash after r2 missiles weight" ) );
				}

				const auto gap = mm->AddMenu( xorstr_( "header_gap" ), xorstr_( "Closing the first gap" ) );
				{
					GapEW = gap->AddCheckBox( xorstr_( "gap_e_w" ), xorstr_( "E[+?]->[?[+W]]" ), true );
					GapEQ = gap->AddCheckBox( xorstr_( "gap_e_q" ), xorstr_( "E[+?]->[?[+Q]]" ), true );
					GapEAA = gap->AddCheckBox( xorstr_( "gap_e_aa" ), xorstr_( "E[+?]->[AA]" ), true );
					GapQ = gap->AddCheckBox( xorstr_( "gap_q" ), xorstr_( "Q->[?]" ), true );
				}

				const auto combat = mm->AddMenu( xorstr_( "header_combat" ), xorstr_( "Combat" ) );
				{
					EDoubleCastWeight = combat->AddSlider( xorstr_( "e_double_cast_weight" ), xorstr_( "Close combat double cast weight" ) );
					EAA = combat->AddKeyBind( xorstr_( "e_aa" ), xorstr_( "Consider E[+?]->[AA->[?]]" ), 'T', false, true );
					R1 = combat->AddKeyBind( xorstr_( "r1" ), xorstr_( "Use R1 in next combo" ), 'C', false, true );
					SCancel = combat->AddCheckBox( xorstr_( "scancel" ), xorstr_( "Use S Cancel" ), true );

					MenuSDK::PermaShow::AddComponent( xorstr_( "Consider E[+?]->[AA->[?]]" ), EAA );
					MenuSDK::PermaShow::AddComponent( xorstr_( "Use R1 in next combo" ), R1 );
					MenuSDK::PermaShow::AddComponent( xorstr_( "Use flash combo" ), Flash );
				}
			}
		}
	}
}
