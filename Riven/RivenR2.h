#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::R2
		{
			constexpr std::uint32_t SpellHash = HASH( "RivenIzunaBlade" );
			constexpr std::uint32_t BuffHash = HASH( "rivenwindslashready" );

			inline bool IsActive = false;
			inline float LastCastTime = 0.f;

			bool IsReady( float delta );
			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo );
			void OnBuffGain( Riot::Spell::Buff::BuffInstance* Buff );
			void OnBuffLose( Riot::Spell::Buff::BuffInstance* Buff );
			Riot::Spell::SpellDataInst* GetSpell();
			float Damage( Riot::AIBaseClient* Object );
		}
	}
}
