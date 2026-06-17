#pragma once
#include "SDK.h"
#include "Menu.h"
#include "BaseSpell.h"
#include "OrbwalkerData.h"
#include "TargetSelector.h"
#include "Prediction.h"
#include "DamageLib.h"
#include "Graphics.h"
#include "ChampionShared.h"
#include "OrbwalkerSDK.h"
#include "NavigationMesh.h"
#include "Evade.h"
#include "HealthPrediction.h"

#include <algorithm>

namespace Features::Champions::Lulu
{
	inline BaseSpell Q, W, WBeam, E, R;

	class TrackedBuff
	{
	public:
		SkinHash Champion = SkinHash::UNKNOWNHASH;
		uint32_t Hash = 0;
		std::string DisplayName;

		MenuSDK::MultiList* MenuEntry = nullptr;

		bool CanW() const
		{
			if ( !this->MenuEntry )
				return false;

			return this->MenuEntry->Values[ 0 ];
		}

		bool CanE() const
		{
			if ( !this->MenuEntry )
			{
			//	Globals::Print( xorstr_( "Menu is null" ) );
				return false;
			}


		//	Globals::Print( xorstr_( "Menu non-null" ) );

			return this->MenuEntry->Values[ 1 ];
		}

		void InitializeMenu( MenuSDK::Menu* Menu, const std::vector<unsigned>& DefaultValue )
		{
			MenuEntry = Menu->AddMultiList( std::to_string( Hash ).c_str(), DisplayName.c_str(), { xorstr_( "Use [W]" ), xorstr_( "Use [E]" ) }, DefaultValue );
		}

		TrackedBuff( SkinHash Owner, uint32_t Hash, const std::basic_string<char>& Display ) : Champion( Owner ),
		                                                                                       Hash( Hash ),
		                                                                                       DisplayName( Display )
		{
		}
	};

	inline AIBaseClient* Pix;

	namespace Menu
	{
		// Q
		inline MenuSDK::CheckBox* ComboQ;
		inline MenuSDK::MultiList* ComboQLocation; // Lulu / Pix

		// W
		inline MenuSDK::MultiList* ComboW; // Speed / Poly

		inline MenuSDK::Info* BoostSpacer;
		inline MenuSDK::CheckBox* ChaseLogic;
		inline MenuSDK::KeyBind* SemiBoost;

		inline MenuSDK::Info* PolySpacer;
		inline MenuSDK::CheckBox* PolySuperiority;
		inline MenuSDK::CheckBox* PolyMarksman;
		inline MenuSDK::CheckBox* PolyBruiser;
		inline MenuSDK::CheckBox* PolyBurst;
		inline MenuSDK::CheckBox* PolyMelee;

		// E
		inline MenuSDK::MultiList* ComboE; // Shield / Damage

		inline MenuSDK::Info* ShieldSpacer;
		inline MenuSDK::CheckBox* BlockDamage;
		inline MenuSDK::Slider* HeliaLogic;
		inline MenuSDK::Menu* UseShieldMenu;
		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseShieldOn;

		inline MenuSDK::Info* DamageSpacer;

		// R
		inline MenuSDK::CheckBox* ComboR;
		inline MenuSDK::Menu* UseRMenu;
		inline std::map<std::uint32_t, MenuSDK::CheckBox*> UseROn;
		inline MenuSDK::CheckBox* OnKillableR;
		inline MenuSDK::CheckBox* AntiGapCloseR;
		inline MenuSDK::Slider* AOER;

		// Automatic
		inline MenuSDK::CheckBox* CancelW;
		inline MenuSDK::Slider* CancelWValue;

		inline MenuSDK::CheckBox* CancelR;
		inline MenuSDK::Slider* CancelRValue;

		// Draw
		inline MenuSDK::CheckBox* DrawQ;
		inline MenuSDK::CheckBox* DrawE;
		// Hitchance
		inline MenuSDK::List* HitchanceQ = nullptr;
	}

	void Initialize();
	void Destroy();

	inline TrackedBuff AsheQ = { SkinHash::Ashe, HASH( "AsheQAttack" ), xorstr_( "Ashe [Q] Buff" ) };
	inline TrackedBuff TwitchQAS = { SkinHash::Twitch, HASH( "twitchhideinshadowsbuff" ), xorstr_( "Twitch [Q] AS Buff" ) };
	inline TrackedBuff TwitchR = { SkinHash::Twitch, HASH( "TwitchFullAutomatic" ), xorstr_( "Twitch [R]" ) };
	inline TrackedBuff JinxP = { SkinHash::Jinx, HASH( "jinxpassivekill" ), xorstr_( "Jinx [P]" ) };
	inline TrackedBuff ZeriR = { SkinHash::Zeri, HASH( "ZeriR" ), xorstr_( "Zeri [R]" ) };
	inline TrackedBuff KogW = { SkinHash::KogMaw, HASH( "KogMawBioArcaneBarrage" ), xorstr_( "Kog'Maw [W]" ) };
	inline TrackedBuff VayneR = { SkinHash::Vayne,HASH( "VayneInquisiton" ), xorstr_( "Vayne [R]" ) };
	inline TrackedBuff SamiraR = { SkinHash::Samira, HASH( "SamiraR" ), xorstr_( "Samira [R]" ) };
	inline TrackedBuff XayahW = { SkinHash::Xayah, HASH( "XayahW" ), xorstr_( "Xayah [W]" ) };
	inline TrackedBuff KaisaE = { SkinHash::Kaisa, HASH( "kaisaeattackspeed" ), xorstr_( "Kai'Sa [E] AS Buff" ) };

	inline TargetSelector::Instance* TS;

	inline float LastCastTime[ 4 ] = { 0.f, 0.f, 0.f, 0.f };
}
