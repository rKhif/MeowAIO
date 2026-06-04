#include "RivenMain.h"

#include "OrbwalkerDefines.h"
#include "RivenR2.h"
#include "RivenR1.h"
#include "RivenAI.h"
#include "RivenDraw.h"
#include "RivenE.h"
#include "RivenFlash.h"
#include "RivenMenu.h"

namespace Features::Champions
{
	namespace Riven
	{
		void OnBuffGainLose( Riot::AIBaseClient* Sender, Riot::Spell::Buff::BuffInstance* Buff, bool IsGain )
		{
			if ( !Sender->IsMe() )
				return;

			if ( IsGain )
			{
				Spell::R2::OnBuffGain( Buff );
			}
			else
			{
				Spell::R1::OnBuffLose( Buff );
				Spell::R2::OnBuffLose( Buff );
			}
		}

		void OnProcessSpell( Riot::AIBaseClient* Sender, Riot::Spell::SpellCastInfo* CastInfo )
		{
			if ( !Sender->IsMe() )
				return;

			Spell::E::OnProcessSpell( CastInfo );
			Spell::R1::OnProcessSpell( CastInfo );
			Spell::R2::OnProcessSpell( CastInfo );
			Core::AI::OnProcessSpell( CastInfo );
		}

		void OnCreateObject( Riot::GameObject* Object )
		{
			Core::AI::OnCreateObject( Object );
		}

		void OrbPreTick()
		{
			Core::AI::GetAction();
		}

		void OnOrbwalkerAction( Features::Orbwalker::ActionArgs* Args, bool* Process )
		{
			if ( Args->Type != Features::Orbwalker::ActionArgs::After )
			{
				return;
			}

			Core::AI::OnAfterAttack( Args->Target );
		}

		void OnDrawHud()
		{
			Draw::OnDraw();
		}

		void Initialize()
		{
			Menu::Initialize();
			Core::AI::Initialize();

			EventHandler::Add( Event::OnProcessSpell, OnProcessSpell );
			EventHandler::Add( Event::OnCreateObject, OnCreateObject );
			EventHandler::Add( Event::OnBuffGainLose, OnBuffGainLose );
			EventHandler::Add( Event::OnPreTick, OrbPreTick );
			EventHandler::Add( Event::OnOrbwalkerAction, OnOrbwalkerAction );
			EventHandler::Add( Event::HudDraw, OnDrawHud );
		}
	}
}
