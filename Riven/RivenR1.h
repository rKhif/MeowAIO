#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::R1
		{
			constexpr std::uint32_t NameHash = HASH( "RivenFengShuiEngine" );

			inline bool IsActive = false;
			inline float LastCastTime = 0.f;

			bool IsReady( float delta );
			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo );
			void OnBuffLose( Riot::Spell::Buff::BuffInstance* Buff );
			Riot::Spell::SpellDataInst* GetSpell();
		}
	}
}
