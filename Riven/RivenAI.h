#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Core::AI
		{
			constexpr float DOUBLE_CAST_TIMEOUT = 1.050f;

			bool CanDoubleCast();
			bool OnAfterAttack( Riot::AttackableUnit* Target );
			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo );
			void OnCreateObject( Riot::GameObject* Object );
			void GetAction();

			void Initialize();
		}
	}
}
