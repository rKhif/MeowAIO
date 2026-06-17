#include "Thresh.h"

#include "GameClient.h"

namespace Features::Champions::Thresh
{
#pragma region Defines

	BaseSpell Q, W, E, EPull, R;

	TargetSelector::Instance* TS;

	float QCost = 70.f;
	float WCost[ 6 ] = { 0.f, 50.f, 55.f, 60.f, 65.f, 70.f };
	float ECost[ 6 ] = { 0.f, 60.f, 65.f, 70.f, 75.f, 80.f };
	float RCost = 100.f;

	AIBaseClient* MarkedTarget = nullptr;

	Vector3f LanternPosition = { 0, 0, 0 };
	double LanternSpawnTime = -5;
	bool AADisabled = false;

	float MinRangeChange = -2.f;
	float MaxRangeChange = -2.f;

	float LastCastTime[ 4 ] = { 0, 0, 0, 0 };

	using BuffType = Spell::Buff::BuffType;

	std::vector HardCC = {
		// Excluding KnockBack
		BuffType::Asleep,
		BuffType::Berserk,
		BuffType::Charm,
		BuffType::Drowsy,
		BuffType::Fear,
		BuffType::Flee,
		BuffType::Knockup,
		BuffType::Stun,
		BuffType::Suppression,
		BuffType::Taunt
	};

#pragma endregion Defines

#pragma region Helpers

	bool CanQ( AIBaseClient* Client )
	{
		return Client && Menu::UseQOn.contains( Client->NetworkID() ) && Menu::UseQOn[ Client->NetworkID() ]->Value;
	}

	bool CanW( AIBaseClient* Client )
	{
		return Client && Menu::UseWOn.contains( Client->NetworkID() ) && Menu::UseWOn[ Client->NetworkID() ]->Value;
	}

	bool IsQ2()
	{
		if ( strcmp( Q.Name().c_str(), xorstr_( "ThreshQLeap" ) ) == 0 )
			return true;
		return false;
	}

	bool IsQSuccessful()
	{
		for ( const auto& Entity : ManagerTemplates::Heroes() )
		{
			if ( !Entity->IsDead() && Entity->IsEnemy() && Entity->BuffManager()->HasBuff( { HASH( "threshqfakeknockup" ), HASH( "ThreshQ" ) } ) )
			{
				return true;
			}
		}
		return false;
	}

	void EToggleCallback( bool value )
	{
		if ( Menu::ModeE->Value == 0 )
			Menu::ModeE->Value = 1;
		else
			Menu::ModeE->Value = 0;
	}

	void MinRangeCallback( MenuSDK::Slider* Slider, int Value )
	{
		MinRangeChange = GlobalClocks::LastUpdateNetworkTime();
	}

	void MaxRangeCallback( MenuSDK::Slider* Slider, int Value )
	{
		MaxRangeChange = GlobalClocks::LastUpdateNetworkTime();
	}

	void Push( AIBaseClient* Target )
	{
		if ( !Target || GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 2 ] < 0.2f )
			return;

		if ( Prediction::PredictionResult EPred; PredictionSuccess( GetPrediction(Target, E, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &EPred) ) && EPred.Hitchance >= Prediction::Medium )
		{
			( void )E.CastSpell( EPred.CastPosition );
			LastCastTime[ 2 ] = GlobalClocks::LastUpdateNetworkTime();
		}
	}

	void Pull( AIBaseClient* Target )
	{
		if ( !Target || !Target->IsValidTarget( E.Range - 25.f, true, ObjectManager::GetPlayer()->Position() ) || GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 2 ] < 0.2f )
			return;

		if ( Prediction::PredictionResult EPred; PredictionSuccess( GetPrediction(Target, EPull, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &EPred) ) && EPred.Hitchance >= Prediction::Medium )
		{
			( void )E.CastSpell( Target->Position().Extend( ObjectManager::GetPlayer()->Position(), 1.2f * E.Range ) );
			LastCastTime[ 2 ] = GlobalClocks::LastUpdateNetworkTime();
		}
	}

	[[maybe_unused]] std::vector DisplacementHashes = {
		HASH( "BlindMonkRKick" ),      // Lee R
		HASH( "headbutttarget" ),      // Alistar W
		HASH( "rocketgrab2" ),         // Blitzcrank Q
		HASH( "PykeQRange" ),          // Pyke Q
		HASH( "TristanaR" ),           // Tristana R
		HASH( "VayneCondemnMissile" ), // Vayne E
	};

	bool CastLantern( AIBaseClient* Client )
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
		const Vector3f TargetPosition = Client->Position();
		const float DistanceToTarget = PlayerPosition.Distance2D( TargetPosition );
		const Vector3f CastPosition = PlayerPosition.Extend( TargetPosition, min( W.Range, DistanceToTarget - 30.f ) );

		return W.CastSpell( CastPosition );
	}

	bool CastLanternPos( Vector3f Position )
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
		const float DistanceToTarget = PlayerPosition.Distance2D( Position );
		const Vector3f CastPosition = PlayerPosition.Extend( Position, min( W.Range, DistanceToTarget - 30.f ) );

		return W.CastSpell( CastPosition );
	}

	void IterateLanternLogic()
	{
		if ( !W.IsReady() )
			return;

		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		for ( const auto& Entity : ManagerTemplates::Heroes() )
		{
			if ( Entity->IsDead() || Entity->IsEnemy() || Entity->IsMe() || !Menu::UseWOn[ Entity->NetworkID() ]->Value )
				continue;

			const float DistanceToEntity = PlayerPosition.Distance2D( Entity->Position() );

			if ( DistanceToEntity >= W.Range + 200.f )
				continue;

			// Q2 Succesful
			//
			if ( IsQSuccessful() && Menu::LanternQ2->Value )
			{
				if ( CastLantern( Entity ) )
					return;
			}

			// Client is HardCC
			//
			if ( Menu::LanternCC->Value )
			{
				for ( const auto& Buff : HardCC )
				{
					if ( Entity->HasBuffOfType( Buff ) && !Entity->HasBuffOfType( BuffType::Knockback ) )
					{
						if ( CastLantern( Entity ) )
							return;
					}
				}
			}

			// Knockback Handler
			//
			if ( Menu::LanternDisplacements->Value )
			{
				if ( Entity->BuffManager()->HasBuff( DisplacementHashes ) )
				{
					const auto Controller = Entity->GetPathController();

					if ( Controller->HasNavigationPath() )
					{
						const auto Path = Controller->GetNavigationPath();
						const Vector3f EndPosition = Path->EndPosition;

						CastLanternPos( EndPosition );
					}
				}
			}
		}
	}

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

	bool ThrowLantern( const Vector3f Position )
	{
		if ( Position.Distance2D( ObjectManager::GetPlayer()->Position() ) > W.Range + 175.f )
			return false;

		if ( W.CastSpell( ObjectManager::GetPlayer()->Position().Extend( Position, min( Position.Distance(ObjectManager::GetPlayer()->Position()), W.Range ) ) ) )
			return true;
		return false;
	}

	void CheckLanternTimeout()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() - LanternSpawnTime >= 6.f )
		{
			LanternPosition = { 0, 0, 0 };
		}
	}

	void NonNewPathAntiGap()
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		for ( const auto& Target : TS->GetOrderedTargets() )
		{
			if ( !Target->IsValidTarget( E.Range, true, PlayerPosition ) )
				continue;

			const auto PathController = Target->GetPathController();
			const bool IsDashing = PathController && PathController->IsDashing() && PathController->HasNavigationPath();

			if ( !IsDashing )
				continue;

			const Vector3f CurrentPosition = Target->Position();
			const Vector3f DashEndPosition = PathController->GetNavigationPath()->EndPosition;
			const float Distance = CurrentPosition.Distance2D( DashEndPosition );

			if ( E.IsReady() && Distance >= 100.f )
			{
				// Distance is handled in Pull and Push funcs
				if ( PlayerPosition.Distance( CurrentPosition ) > PlayerPosition.Distance( DashEndPosition ) )
				{
					// Dash Away
					Pull( Target );
				}
				else
				{
					// Dash In
					Push( Target );
				}
			}
		}
	}

#pragma endregion Helpers

#pragma region Orbwalker

	void Combo()
	{
		const bool ShouldDisableAA = ( E.IsReady() && !AADisabled && Menu::DisableAA->Value ) || ObjectManager::GetPlayer()->BuffManager()->HasBuff( HASH( "ThreshQPullMissile" ) );

		if ( ShouldDisableAA )
		{
			Orbwalker::SDK::SetAttackPause( 1 );
		}
		else
		{
			Orbwalker::SDK::SetAttackPause( 0 );
		}

		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		const std::function ETargetFilter = [] ( AIBaseClient* Target )
		{
			return !Target->mCharacterState()->IsUnstoppable();
		};

		const std::function EPredFilter = [] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionE )
		{
			if ( !PredictionSuccess( Status ) )
				return false;

			return PredictionE->Hitchance >= Prediction::Medium && !HasSpellShield( Target, PredictionE->TimeToTarget );
		};

		const bool IsPull = Menu::ModeE->Value == 0;
		const auto Spell = IsPull ? EPull : E;

		if ( Menu::ComboE->Value )
		{
			for ( const auto& [ Client, Prediction ] : TS->GetBestTargets( Spell, PlayerPosition, ETargetFilter, EPredFilter ) )
			{
				if ( IsQSuccessful() || ( GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 0 ] < ( Q.Range / Q.Speed ) + Q.CastDelay && !IsQSuccessful() ) )
					break;

				Vector3f CastPosition = Prediction.CastPosition;

				if ( IsPull )
				{
					CastPosition = CastPosition.Extend( PlayerPosition, PlayerPosition.Distance2D( CastPosition ) * 2.f );
				}

				if ( E.CastSpell( CastPosition ) )
					return;
			}
		}

		const std::function QTargetFilter = [&] ( AIBaseClient* Target )
		{
			const float DistanceToPlayer = Target->Position().Distance2D( PlayerPosition );
			return !Target->mCharacterState()->IsUnstoppable() && DistanceToPlayer > Menu::MinQRange->Value && DistanceToPlayer < Menu::MaxQRange->Value && CanQ( Target );
		};

		const std::function QPredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionQ )
		{
			if ( !PredictionSuccess( Status ) )
				return false;

			return !HasSpellShield( Target, PredictionQ->TimeToTarget );
		};

		if ( IsQ2() )
			return;

		if ( Menu::ComboQ->Value )
		{
			for ( const auto& [ Client, Prediction ] : TS->GetBestTargets( Q, PlayerPosition, QTargetFilter, QPredFilter ) )
			{
				if ( Prediction.Hitchance >= GetHitchance( Menu::HitchanceQ ) && Q.CastSpell( Prediction.CastPosition ) )
				{
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					return;
				}
			}
		}
	}

#pragma endregion Orbwalker

#pragma region Events

	void OnTick()
	{
		NonNewPathAntiGap();
		CheckLanternTimeout();
		IterateLanternLogic();

		switch ( Orbwalker::ActiveMode )
		{
		case Orbwalker::Mode::Combo:
			Combo();
			break;
		default:
			Orbwalker::SDK::SetAttackPause( 0 );
			break;
		}
	}

	void OnDraw()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() - MinRangeChange < 1.5f )
		{
			Graphics::AddCircle( ObjectManager::GetPlayer()->Position(), Menu::MinQRange->Value, 2, Graphics::RGBA( 211, 211, 211, 211 ) );
		}

		if ( GlobalClocks::LastUpdateNetworkTime() - MaxRangeChange < 1.5f )
		{
			Graphics::AddCircle( ObjectManager::GetPlayer()->Position(), Menu::MaxQRange->Value, 2, Graphics::RGBA( 211, 211, 211, 211 ) );
		}

		if ( Menu::DrawMode->Value )
		{
			Vector2f PlayerScreenPosition;
			if ( !r3dProjectToScreen( ObjectManager::GetPlayer()->Position(), PlayerScreenPosition ) )
				return;

			const char* DirectionW = Menu::ModeE->Value == 1 ? xorstr_( "[E] Direction: Push" ) : xorstr_( "[E] Direction: Pull" );
			const Vector2f DirectionWSize = Graphics::TextSize( DirectionW, 22, true );

			PlayerScreenPosition.y += 28 - DirectionWSize.y / 2;
			PlayerScreenPosition.x -= DirectionWSize.x / 2;
			Graphics::AddTextStroke( DirectionW, 22, PlayerScreenPosition, Graphics::RGBA( 71, 135, 120, 255 ), true );
		}

		if ( LanternPosition.x > 1 )
		{
			DrawProgress( LanternPosition, 100, 5, 200, std::clamp( ( ( GlobalClocks::LastUpdateNetworkTime() - static_cast<float>( LanternSpawnTime ) ) / 6.f ) * 360.f, 0.f, 359.9f ), Graphics::RGBA( 34, 139, 34, 211 ) );
		}
	}

	void OnNewPath( AIBaseClient* Sender, std::vector<Vector3f>* Nodes, bool IsDash, float DashSpeed )
	{
		if ( IsDash && Sender->IsEnemy() && Nodes->size() >= 2 )
		{
			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
			const Vector3f EndPosition = Nodes->back();
			const float Distance = Nodes->front().Distance2D( Nodes->back() );

			if ( E.IsReady() && Distance >= 250.f )
			{
				// Distance is handled in Pull and Push funcs
				if ( PlayerPosition.Distance( Nodes->back() ) > PlayerPosition.Distance( Nodes->front() ) )
				{
					// Dash Away
					Pull( Sender );
				}
				else
				{
					// Dash In
					Push( Sender );
				}
			}

			if ( Menu::LanternGapCloser->Value )
			{
				// Lantern on Gapclose
				for ( const auto& Entity : ManagerTemplates::Heroes() )
				{
					if ( !Entity->IsDead() && CanW( Entity ) && Entity->IsAlly() && Entity->Distance( ObjectManager::GetPlayer() ) <= W.Range + 300 && Entity->Distance( ObjectManager::GetPlayer() ) > 200.f && EndPosition.Distance2D( Entity->Position() ) <= 250.f && Distance >= 300 && Sender->SkinHash() != SkinHash::Yasuo && ThrowLantern( Entity->Position() ) )
					{
						return;
					}
				}
			}
		}
	}

	void OnWorldDraw()
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		if ( Menu::DrawQ->Value && Q.IsReady() )
			Graphics::AddCircle( PlayerPosition, Menu::MaxQRange->Value, 1, Menu::ColorQ->Value );

		if ( Menu::DrawE->Value && E.IsReady() )
			Graphics::AddCircle( PlayerPosition, E.Range, 1, Menu::ColorE->Value );
	}

	void OnCreate( GameObject* Sender )
	{
		if ( Sender->Name() == xorstr_( "ThreshLantern" ) )
		{
			Vector3f Pos = Sender->Position();
			GroundVector( Pos );
			LanternPosition = Pos;
			LanternSpawnTime = GlobalClocks::LastUpdateNetworkTime();
		}
	}

	void OnDelete( GameObject* Sender )
	{
		if ( Sender->Name().contains( xorstr_( "_Lantern_cas_green" ) ) )
		{
			LanternPosition = { 0, 0, 0 };
		}
	}

#pragma endregion Events

	void Initialize()
	{
		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .5f,
			.Range = 1100.f,
			.RangeType = Center,
			.Speed = 1900.f,
			.Radius = 70.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Linear,
			.CastDelay = 0.f,
			.Range = 950.f,
			.Speed = FLT_MAX,
			.Radius = 120.f, // Arbitrary
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Linear,
			.CastDelay = 0.f, // Virtually Arbitrary, not exact
			.Range = 525.f,
			.RangeType = Edge,
			.Speed = 2000.f,
			.Radius = 200.f
		} );

		EPull = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Linear,
			.CastDelay = 0.f, // Virtually Arbitrary, not exact
			.Range = 525.f,
			.RangeType = Edge,
			.Speed = FLT_MAX,
			.Radius = 200.f
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.Range = 450.f
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueThresh" ), xorstr_( "Blue Thresh" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AA );

		const auto MenuQ = Main->AddMenu( xorstr_( "MenuQ" ), xorstr_( "[Q / Death Sentence] Settings" ) );
		MenuQ->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );

		Menu::ComboQ = MenuQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );

		const auto UseQOn = MenuQ->AddMenu( xorstr_( "UseQOn" ), xorstr_( "Use on..." ) );
		for ( const auto& Entity : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Entity ) )
				continue;

			Menu::UseQOn[ Entity->NetworkID() ] = UseQOn->AddCheckBox( Entity->SkinName(), GetDisplayName( Entity ), true );
			Menu::UseQOn[ Entity->NetworkID() ]->AddTexture( Entity->GetIconSquare() );
			Menu::UseQOn[ Entity->NetworkID() ]->Hide( Entity->IsAlly() );
		}

		MenuQ->AddInfo( xorstr_( "Q2Spacer" ), xorstr_( "" ) );
		Menu::ComboQ2 = MenuQ->AddCheckBox( xorstr_( "ComboQ2" ), xorstr_( "Use [Q2] / [Deathly Leap]" ), false );
		Menu::SafeQ2 = MenuQ->AddCheckBox( xorstr_( "Q2Safe" ), xorstr_( "Check for End Position Safety" ), true );

		MenuQ->AddInfo( xorstr_( "SpellSettings" ), xorstr_( "" ) );
		Menu::HitchanceQ = MenuQ->AddList( xorstr_( "HitchanceQ" ), xorstr_( "Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 3 );
		Menu::MinQRange = MenuQ->AddSlider( xorstr_( "MinQRange" ), xorstr_( "Minimum Range" ), 300, 100, 1100, 50, MinRangeCallback );
		Menu::MaxQRange = MenuQ->AddSlider( xorstr_( "MaxQRange" ), xorstr_( "Maximum Range" ), 1100, 450, 1100, 50, MaxRangeCallback );
		Menu::DrawQ = MenuQ->AddCheckBox( xorstr_( "DrawQ" ), xorstr_( "Draw Q Range" ), true );
		Menu::ColorQ = MenuQ->AddColorPicker( xorstr_( "ColorQ" ), xorstr_( "Draw Color" ), Graphics::RGBA( 79, 121, 66, 255 ) );

		const auto MenuW = Main->AddMenu( xorstr_( "lanternMenu" ), xorstr_( "[W / Dark Passage] Settings" ) );
		MenuW->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_W )->GetIcon() );

		Menu::ThrowLantern = MenuW->AddKeyBind( xorstr_( "throwLantern" ), xorstr_( "Throw to Nearest Ally" ), 'G', false, false );
		const auto WMenu = MenuW->AddMenu( xorstr_( "useWOn" ), xorstr_( "Use [W] / [Dark Passage] on:" ) );
		for ( const auto& Entity : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Entity, true ) )
				continue;

			Menu::UseWOn[ Entity->NetworkID() ] = WMenu->AddCheckBox( Entity->SkinName(), GetDisplayName( Entity ), true );
			Menu::UseWOn[ Entity->NetworkID() ]->AddTexture( Entity->GetIconSquare() );
			Menu::UseWOn[ Entity->NetworkID() ]->Hide( Entity->IsEnemy() );
		}

		MenuW->AddInfo( xorstr_( "lanternSeparator" ), xorstr_( "Throw Lantern ..." ) );
		Menu::LanternQ2 = MenuW->AddCheckBox( xorstr_( "lanternQ2" ), xorstr_( "... if [Q] / [Death Sentence] succesful" ), true );
		Menu::LanternDamage = MenuW->AddCheckBox( xorstr_( "lanternDamage" ), xorstr_( "... if Ally Incoming Damage >= x%" ) );
		Menu::LanternDamagePerc = MenuW->AddSlider( xorstr_( "lanternDamagePerc" ), xorstr_( " ^- x = ?" ), 15, 10, 100, 5 );
		Menu::LanternDisplacements = MenuW->AddCheckBox( xorstr_( "lanternDisplacements" ), xorstr_( "... on Ally Displacement" ), true );
		Menu::LanternGapCloser = MenuW->AddCheckBox( xorstr_( "lanternGapcloser" ), xorstr_( "... on Enemy Gap Closer" ), true );
		Menu::LanternCC = MenuW->AddCheckBox( xorstr_( "lanternCC" ), xorstr_( "... on Ally CC" ), true );
		Menu::LanternAmountOfEnemiesAroundAlly = MenuW->AddSlider( xorstr_( "lanternAmtEnemies" ), xorstr_( "... if >= x Enemies around Ally" ), 3, 0, 6 );

		const auto MenuE = Main->AddMenu( xorstr_( "MenuE" ), xorstr_( "[E / Flay] Settings" ) );
		MenuE->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );

		Menu::ComboE = MenuE->AddCheckBox( xorstr_( "comboE" ), xorstr_( "Use [E] / [Flay]" ), true );
		Menu::ModeE = MenuE->AddList( xorstr_( "modeE" ), xorstr_( "[E] / [Flay] Mode" ), { xorstr_( "Pull" ), xorstr_( "Push" ) }, 0 );
		Menu::ToggleE = MenuE->AddKeyBind( xorstr_( "toggleE" ), xorstr_( "Change [E] / [Flay] Mode" ), 'T', false, true, EToggleCallback );
		Menu::DisableAA = MenuE->AddCheckBox( xorstr_( "disableAA" ), xorstr_( "Disable [AA] if [E] / [Flay] is Ready" ), true );
		Menu::ForceEQ = MenuE->AddCheckBox( xorstr_( "forceEQ" ), xorstr_( "Force [E] / [Flay] into [Q] / [Death Sentence]" ), false );

		MenuE->AddInfo( xorstr_( "SpacerDraw" ), xorstr_( "" ) );
		Menu::DrawMode = MenuE->AddCheckBox( xorstr_( "DrawModeE" ), xorstr_( "Draw E Mode" ), true );
		Menu::DrawE = MenuE->AddCheckBox( xorstr_( "DrawE" ), xorstr_( "Draw E Range" ), true );
		Menu::ColorE = MenuE->AddColorPicker( xorstr_( "ColorE" ), xorstr_( "Draw Color" ), Graphics::RGBA( 95, 158, 160, 255 ) );

		const auto MenuR = Main->AddMenu( xorstr_( "MenuR" ), xorstr_( "[R / The Box] Settings" ) );
		MenuR->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->GetIcon() );

		Menu::ComboR = MenuR->AddCheckBox( xorstr_( "ComboR" ), xorstr_( "Use in Combo" ), false );

		EventHandler::Add( Event::OnGameUpdate, OnTick );
		EventHandler::Add( Event::Present, OnDraw );
		EventHandler::Add( Event::WorldDraw, OnWorldDraw );
		EventHandler::Add( Event::OnCreateObject, OnCreate );
		EventHandler::Add( Event::OnDeleteObject, OnDelete );
		EventHandler::Add( Event::OnNewPath, OnNewPath );

		Globals::Print( xorstr_( "Blue Thresh Loaded!" ) );
	}

	void Destroy()
	{
	}
}
