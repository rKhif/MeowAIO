#include "Soraka.h"
#include <ranges>

namespace Features::Champions::Soraka
{
	std::vector<AIHeroClient*> ViableTargets;

	Prediction::Hitchance GetHitchance( const MenuSDK::List* Menu )
	{
		if ( !Menu )
			return Prediction::DashingEnd; // This is to make sure the result is visible xd
		switch ( Menu->Value )
		{
		case 0:
			return Prediction::VeryLow;
		case 1:
			return Prediction::Low;
		case 2:
			return Prediction::Medium;
		case 3:
			return Prediction::High;
		case 4:
			return Prediction::VeryHigh;
		case 5:
			return Prediction::DashingMidAir;
		default:
			return Prediction::Medium;
		}
	}

	void ClearRefillCache()
	{
		ViableTargets.clear();
		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client->IsDead() || !Client->IsVisible() || !Client->IsTargetable() )
				continue;

			ViableTargets.emplace_back( Client );
		}
	}

	void CancelHelper()
	{
		if ( !E.IsReady() || !Menu::ModeE->Values[ 0 ] )
			return;

		for ( const auto& Client : ViableTargets )
		{
			if ( Client->IsEnemy() && CanCancel( E, Client, 1 ) )
			{
				Prediction::PredictionResult Result;
				if ( PredictionSuccess( GetPrediction(Client, E, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &Result) ) && Result.Hitchance >= Prediction::Medium && E.CastSpell( Result.CastPosition ) )
					return;
			}
		}
	}

	bool MidAirQ = false;

	bool PassesCheckW( AIBaseClient* Client = nullptr )
	{
		if ( !W.IsReady() || !Menu::ToggleW->Value )
			return false;

		if ( !Client )
		{
			if ( ObjectManager::GetPlayer()->GetResourcePercentage() < static_cast<float>( Menu::MinManaW->Value ) )
				return false;

			if ( ObjectManager::GetPlayer()->FindBuff( HASH( "sorakaqregen" ) && Menu::PassiveW->Value ) )
				return true;

			if ( Menu::ComboW->Value && Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
				return false;

			return true;
		}

		if ( MidAirQ && Client->GetHealthPercentage() > 33.f )
			return false;

		return Menu::WhitelistW.contains( Client->NetworkID() ) && Menu::WhitelistW[ Client->NetworkID() ]->CanHeal();
	}

	void HealHandler()
	{
		const bool PassesChecks = PassesCheckW();

		if ( !PassesChecks )
			return;

		for ( const auto& Client : ViableTargets )
		{
			if ( Client->IsEnemy() )
				continue;

			if ( W.IsReady() && PassesCheckW( Client ) && W.CastSpell( Client ) )
			{
				return;
			}

			const float IncomingDamage = Damage::GetIncomingDamage( Client, 2.f );
			const bool ShouldR = Client->GetHealth() - IncomingDamage <= Client->GetMaxHealth() * 0.1f && IncomingDamage > 1.f && EnemyHeroTargetsInRange( Client, 1500.f * 1500.f, 1.f ) >= 1;

			if ( ShouldR && R.IsReady() && Menu::ToggleR->Value && Menu::WhitelistR.contains( Client->NetworkID() ) && Menu::WhitelistR[ Client->NetworkID() ]->Value && R.CastSpell() )
				return;
		}
	}

	void SnareHandler()
	{
		if ( !E.IsReady() || !Menu::ModeE->Values[ 1 ] )
			return;

		for ( const auto& Client : ViableTargets )
		{
			if ( Client->IsAlly() )
				continue;

			bool IsSnareable = false;

			Prediction::PredictionResult Result;
			if ( PredictionSuccess( GetPrediction(Client, SnareE, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &Result) ) && Result.Hitchance > Prediction::VeryHigh )
				IsSnareable = true;

			if ( IsSnareable && E.CastSpell( Result.CastPosition ) )
				return;
		}
	}

	void PokeHandler()
	{
		const auto Current = Orbwalker::ActiveMode;

		if ( Current != Orbwalker::Mode::Combo && Current != Orbwalker::Mode::Harass )
			return;

		const bool HarassQ = ObjectManager::GetPlayer()->GetResourcePercentage() >= Menu::MinManaW->Value;
		const bool HarassE = ( ( Menu::HarassE->Value && Current == Orbwalker::Mode::Harass ) || ( Menu::ComboE->Value && Current == Orbwalker::Mode::Combo ) ) && E.IsReady();

		for ( const auto& Client : TS->GetOrderedTargets() )
		{
			Prediction::PredictionResult Result;

			if ( PredictionSuccess( GetPrediction(Client, E, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &Result) ) )
			{
				if ( HarassE && Result.Hitchance >= Prediction::High && E.CastSpell( Result.CastPosition ) )
					return;

				if ( Menu::ModeE->Values[ 2 ] && Result.Hitchance == Prediction::DashingEnd && E.CastSpell( Result.CastPosition ) )
					return;
			}

			if ( HarassQ && PredictionSuccess( GetPrediction(Client, Q, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &Result) ) && Result.Hitchance >= GetHitchance( Menu::HitChanceQ ) )
			{
				( void )Q.CastSpell( Result.CastPosition );
			}
		}
	}

	void OnGameUpdate()
	{
		ClearRefillCache();
		CancelHelper();
		HealHandler();
		SnareHandler();
		PokeHandler();
	}

	void OnDrawHud()
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		if ( Menu::DrawQ->Value && Q.IsReady() )
			Graphics::AddCircle( PlayerPosition, Q.Range, 1, Graphics::RGBA( 228, 155, 15, 211 ) );

		if ( Menu::DrawW->Value && W.IsReady() && Menu::ToggleW->Value )
			Graphics::AddCircle( PlayerPosition, W.Range, 1, Graphics::RGBA( 80, 200, 120, 211 ) );

		if ( Menu::DrawE->Value && E.IsReady() )
			Graphics::AddCircle( PlayerPosition, E.Range, 1, Graphics::RGBA( 224, 176, 255, 211 ) );

		Vector2f ScreenPosition;

		if ( !r3dProjectToScreen( PlayerPosition, ScreenPosition ) )
			return;

		const std::string Current = Menu::ToggleW->Value ? xorstr_( "[W] is Enabled" ) : xorstr_( "[W] is Disabled" );
		const DWORD Color = Menu::ToggleW->Value ? Graphics::RGBA( 42, 170, 138, 255 ) : Graphics::RGBA( 129, 133, 137, 120 );

		const Vector2f TextSize = Graphics::TextSize( Current.c_str(), 18, true );
		Graphics::AddText( Current.c_str(), 18, Vector2f{ ScreenPosition.x - TextSize.x / 2.f, ScreenPosition.y + 80.f - TextSize.y - 2.f }, Color, true );
	}

	void OnDrawWorld()
	{
		if ( ObjectManager::GetPlayer()->IsDead() || !W.IsReady() )
			return;

		for ( const auto& Client : Menu::WhitelistW | std::views::values )
		{
			if ( Client->CanHeal() )
				Client->Draw();
		}
	}

	void OnCreate( GameObject* Object )
	{
		Spell::MissileClient* Missile = Object->AsMissile();

		if ( Missile )
		{
			if ( !Missile->GetCaster() || Missile->GetCaster()->SkinHash() != SkinHash::Soraka )
				return;

			const std::string Name = Missile->Name();

			if ( strcmp( xorstr_( "SorakaQMissile" ), Name.c_str() ) == 0 )
			{
				MidAirQ = true;
			}

			if ( strcmp( xorstr_( "SorakaQReturnMissile" ), Name.c_str() ) == 0 )
			{
				MidAirQ = true;
			}
		}
	}

	void OnDelete( GameObject* Object )
	{
		Spell::MissileClient* Missile = Object->AsMissile();

		if ( Missile )
		{
			if ( !Missile->GetCaster() || Missile->GetCaster()->SkinHash() != SkinHash::Soraka )
				return;

			const std::string Name = Missile->Name();

			if ( strcmp( xorstr_( "SorakaQMissile" ), Name.c_str() ) == 0 )
			{
				MidAirQ = false;
			}

			if ( strcmp( xorstr_( "SorakaQReturnMissile" ), Name.c_str() ) == 0 )
			{
				MidAirQ = false;
			}
		}
	}

	void Initialize()
	{
		auto QTimeToTarget = [ ] ( const BaseSpell& Spell, const float Distance ) -> float
		{
			const size_t Amount = floor( Distance / 50.f );
			return Spell.CastDelay + Amount * .05f;
		};

		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.Type = Circular,
			.CastDelay = 0.25f,
			.Range = 800.f,
			.RangeType = Center,
			.CalculateTimeTraveledCustom = QTimeToTarget,
			.Radius = 230.f,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Targeted,
			.CastDelay = 0.25f,
			.Range = 550.f,
			.RangeType = Edge,
			.Speed = 0.f
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Circular,
			.CastDelay = 0.25f,
			.Range = 925.f,
			.RangeType = Center,
			.Speed = 15000.f,
			.Radius = 260.f
		} );

		SnareE = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Circular,
			.CastDelay = 1.75f,
			.Range = 925.f,
			.RangeType = Center,
			.Speed = 0.f,
			.Radius = 260.f
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.Type = Self,
			.CastDelay = 0.25f
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueSoraka" ), xorstr_( "Blue Soraka" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AP );

		const auto MenuQ = Main->AddMenu( xorstr_( "MenuQ" ), xorstr_( "[Q / Starcall] Settings" ) );
		MenuQ->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );
		Menu::ComboQ = MenuQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
		Menu::HarassQ = MenuQ->AddCheckBox( xorstr_( "HarassQ" ), xorstr_( "Use in Harass" ), true );
		Menu::MinManaQ = MenuQ->AddSlider( xorstr_( "MinManaQ" ), xorstr_( "Only if Mana >= #%" ), 35, 0, 100, 5 );
		Menu::HitChanceQ = MenuQ->AddList( xorstr_( "hitchanceR" ), xorstr_( "Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 3 );
		Menu::DrawQ = MenuQ->AddCheckBox( xorstr_( "DrawQ" ), xorstr_( "Draw Range" ), true );

		const auto MenuW = Main->AddMenu( xorstr_( "MenuW" ), xorstr_( "[W / Astral Infusion] Settings" ) );
		MenuW->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_W )->GetIcon() );
		Menu::ToggleW = MenuW->AddKeyBind( xorstr_( "ToggleW" ), xorstr_( "Toggle W Usage" ), VK_F1, true, true );
		Menu::ComboW = MenuW->AddCheckBox( xorstr_( "ComboW" ), xorstr_( "Only use in Combo" ), false );
		Menu::PassiveW = MenuW->AddCheckBox( xorstr_( "PassiveW" ), xorstr_( "Auto-Pass Passive with W if Applicable" ), true );
		Menu::MinManaW = MenuW->AddSlider( xorstr_( "MinManaW" ), xorstr_( "Minimum Mana %" ), 33, 0, 100, 1 );
		Menu::DrawW = MenuW->AddCheckBox( xorstr_( "DrawW" ), xorstr_( "Draw Range" ), true );
		MenuW->AddInfo( xorstr_( "Spacer" ), xorstr_( " " ) );
		const auto WhitelistParent = MenuW->AddMenu( xorstr_( "Whitelist" ), xorstr_( "Whitelist" ) );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client->IsEnemy() || Client->IsMe() )
				continue;

			const auto MenuEntry = WhitelistParent->AddMenu( Client->SkinName(), GetDisplayName( Client ) );
			MenuEntry->AddTexture( Client->GetIconSquare() );

			const auto ToReturn = new HealUnit;
			const double DefaultValue = IsMarksman( Client ) || IsBurst( Client ) ? 75 : IsFighter( Client ) || IsMage( Client ) ? 50 : 25;

			ToReturn->Client = Client;
			ToReturn->Toggle = MenuEntry->AddCheckBox( xorstr_( "Toggle" ), xorstr_( "Enabled" ), DefaultValue > 30.f );
			ToReturn->MinHP = MenuEntry->AddSlider( xorstr_( "MinHP" ), xorstr_( "If HP < #%" ), DefaultValue, 5 );

			Menu::WhitelistW[ Client->NetworkID() ] = ToReturn;
		}

		const auto MenuE = Main->AddMenu( xorstr_( "MenuE" ), xorstr_( "[E / Equinox] Settings" ) );
		MenuE->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );
		Menu::ComboE = MenuE->AddCheckBox( xorstr_( "ComboE" ), xorstr_( "Use in Combo (Poke)" ), false );
		Menu::HarassE = MenuE->AddCheckBox( xorstr_( "HarassE" ), xorstr_( "Use in Harass (Poke)" ), true );
		Menu::ModeE = MenuE->AddMultiList( xorstr_( "ModeE" ), xorstr_( "Auto-Usages" ), { xorstr_( "Cancel" ), xorstr_( "Guaranteed Snare" ), xorstr_( "Anti Gap Close" ) }, { 0, 1, 2 } );
		Menu::DrawE = MenuE->AddCheckBox( xorstr_( "DrawE" ), xorstr_( "Draw Range" ), false );

		const auto CancelMenu = MenuE->AddMenu( xorstr_( "CancelMenu" ), xorstr_( "Spells to Cancel" ) );
		InitializeCancelMenu( CancelMenu );

		const auto MenuR = Main->AddMenu( xorstr_( "MenuR" ), xorstr_( "[R / Wish] Settings" ) );
		MenuR->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->GetIcon() );
		Menu::ToggleR = MenuR->AddCheckBox( xorstr_( "ToggleR" ), xorstr_( "Toggle R Usage" ), true );

		const auto WhitelistR = MenuR->AddMenu( xorstr_( "Whitelist" ), xorstr_( "Whitelist" ) );
		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client->IsEnemy() )
				continue;

			Menu::WhitelistR[ Client->NetworkID() ] = WhitelistR->AddCheckBox( Client->SkinName(), GetDisplayName( Client ), true );
			Menu::WhitelistR[ Client->NetworkID() ]->AddTexture( Client->GetIconSquare() );
		}

		EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );
		EventHandler::Add( Event::WorldDraw, OnDrawWorld );
		EventHandler::Add( Event::HudDraw, OnDrawHud );
		EventHandler::Add( Event::OnCreateObject, OnCreate );
		EventHandler::Add( Event::OnDeleteObject, OnDelete );

		MenuSDK::PermaShow::AddComponent( xorstr_( "Toggle W" ), Menu::ToggleW );
	}
}
