#include "Nami.h"
#include "BuffCache.h"
#include "GameClient.h"

namespace Features::Champions::Nami
{
	BaseSpell Q, W, BounceW, E, R;

	std::vector<std::pair<AIBaseClient*, Spell::Buff::BuffInstance*>> CCList;
	std::vector<std::tuple<AIBaseClient*, float, float>> StasisList;

	constexpr float WSpeedEnemy = 2500.f;
	constexpr float WSpeedAlly = 1500.f;

#pragma region Helpers

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

	void CheckMenus()
	{
		const auto ManagerInstance = ObjectManager::GetInstance();
		for ( const auto& [ NetworkID, MenuEntry ] : Menu::UseQOn )
		{
			GameObject* Object = ManagerInstance->GetNetworkObject( NetworkID );
			if ( !Object || Object->IsAlly() )
			{
				MenuEntry->Hide( true );
				continue;
			}

			MenuEntry->Hide( false );
		}

		for ( const auto& [ NetworkID, UnitEntry ] : Menu::UseESettings )
		{
			GameObject* Object = ManagerInstance->GetNetworkObject( NetworkID );
			const auto MenuEntry = UnitEntry.Menu;

			if ( !Object || Object->IsEnemy() )
			{
				MenuEntry->Hide( true );
				continue;
			}

			MenuEntry->Hide( false );
		}
	}

	const std::function TargetFilter = [&] ( AIBaseClient* Target )
	{
		// Enabled in Menu
		//
		if ( Menu::UseQOn.contains( Target->NetworkID() ) && !Menu::UseQOn[ Target->NetworkID() ]->Value )
			return false;

		// Fallback Bot Check
		//
		if ( Target->AsHero()->IsBot() && GameStartData::GetInstance()->Map() == GameMapId::Arena )
			return false;

		return true;
	};

	const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionQ )
	{
		if ( !PredictionSuccess( Status ) )
			return false;

		// Can't be CC'd
		//
		if ( HasSpellShield( Target, PredictionQ->TimeToTarget ) || PredictionQ->Data.Unstoppable )
			return false;

		const float CrowdControlTime = BuffCache::GetCrowdControlTimeRemaining( Target );
		if ( CrowdControlTime > 1.5f )
			return false;

		// Slowed or doesn't require Slow
		//
		if ( Menu::SlowQ->Value && !Target->HasBuffOfType( BuffType::Slow ) && PredictionQ->Hitchance < Prediction::DashingMidAir )
			return false;

		return true;
	};

	void HandleCancels()
	{
		if ( Q.IsReady() || R.IsReady() )
		{
			for ( const auto& Client : ManagerTemplates::Heroes() )
			{
				if ( Client->IsEnemy() && !Client->IsDead() && Client->SpellBook()->ActiveSpellInstance() && !Client->mCharacterState()->IsUnstoppable() )
				{
					Prediction::PredictionResult Result;
					if ( Q.IsReady() && Menu::CancelQ->Value && CanCancel( Q, Client, Menu::CancelQImportance->Value ) && PredictionSuccess( Prediction::GetPrediction(Client, Q, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &Result) ) && Result.Hitchance >= Prediction::Medium && Q.CastSpell( Result.CastPosition ) )
					{
						return;
					}

					if ( R.IsReady() && Menu::CancelR->Value && CanCancel( R, Client, Menu::CancelRImportance->Value ) && PredictionSuccess( Prediction::GetPrediction(Client, R, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &Result) ) && Result.Hitchance >= Prediction::Medium && R.CastSpell( Result.CastPosition ) )
					{
						return;
					}
				}
			}
		}
	}

	void HandleGapClose()
	{
		if ( !Q.IsReady() || !Menu::GapCloseQ->Value )
			return;

		if ( GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 0 ] <= 0.4f )
			return;

		for ( const auto& [ Client, Prediction ] : TS->GetBestTargets( Q, ObjectManager::GetPlayer()->Position(), TargetFilter, PredFilter ) )
		{
			switch ( Prediction.Hitchance )
			{
			case Prediction::DashingMidAir:
			case Prediction::DashingEnd:
				if ( Q.CastSpell( Prediction.CastPosition ) )
				{
					LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
					return;
				}
			default:
				break;
			}
		}
	}

	constexpr float Heal[ 7 ] = { 0.f, 55.f, 75.f, 95.f, 115.f, 135.f, 155.f };
	constexpr float MinHeal[ 7 ] = { 0.f, 38.5f, 52.5f, 66.5f, 80.5f, 94.5f, 108.5f };

	constexpr float Ratio = .25f;
	constexpr float MinRatio = .175f;

	float GetHealValue( AIBaseClient* Client, bool IsRebound = false )
	{
		// TODO: Implement Grievous Wounds

		if ( IsRebound )
			return MinHeal[ W.Level() ] + MinRatio * ObjectManager::GetPlayer()->TotalAbilityPower();

		return Heal[ W.Level() ] + Ratio * ObjectManager::GetPlayer()->TotalAbilityPower();
	}

#pragma endregion

#pragma region Orbwalker

	void HandleW()
	{
		if ( !W.IsReady() || IsRecalling( ObjectManager::GetPlayer() ) )
			return;

		if ( GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 1 ] <= 0.3f )
			return;

		const bool CanNonHeal = Orbwalker::ActiveMode == Orbwalker::Mode::Combo || Orbwalker::ActiveMode == Orbwalker::Mode::Harass;

		if ( !CanNonHeal && !Menu::HealW->Value )
			return;

		// Bounce Logic
		//
		if ( Menu::BounceW->Value )
		{
			for ( const auto& Client : ManagerTemplates::Heroes() )
			{
				// Need to check if Client is allowed
				//
				if ( Client->IsDead() || !Client->IsTargetable() || Client->Distance( ObjectManager::GetPlayer() ) > W.Range || !Menu::UseWOn[ Client->NetworkID() ]->Value )
					continue;

				// If we want to bounce off of ally, check if it's actually worth it to do so
				//
				if ( Client->IsAlly() && Client->GetHealth() + GetHealValue( Client, false ) > Client->GetMaxHealth() )
					continue;

				// We're allowed to bounce from Client, get bounce targets
				//
				const float TimeToClient = W.CastDelay + Client->Distance( ObjectManager::GetPlayer() ) / W.Speed + .08f; // Get the TimeToClient
				Prediction::PredictionData ClientData;                                                                    // Bounce Unit Prediction
				GetPositionAfterTime( Client, TimeToClient, &ClientData );
				Vector3f ClientPosition = Client->IsMe() ? Client->Position() : ClientData.Position; // We don't want Prediction on self because we'll stop when casting W
				AIBaseClient* Nearest = nullptr;

				float MinDist = 726.f;
				Prediction::PredictionData TemporaryData; // Bounce Unit Prediction
				for ( const auto& Second : ManagerTemplates::Heroes() )
				{
					// Get Nearest ally, this is faster than ranges::sort.first + std::remove_if
					// Check if Client Team != Second Team, if Second is dead, untargetable or non-visible
					if ( Client->Team() == Second->Team() || Second->IsDead() || !Second->IsTargetable() || !Second->IsVisible() )
						continue;

					// Get the position of Second when our W reaches Client
					GetPositionAfterTime( Second, TimeToClient, &TemporaryData );
					Vector3f TemporaryPosition = TemporaryData.Position;

					// Distance Check
					if ( const float Dist = ClientPosition.Distance2D( TemporaryPosition ); Dist <= MinDist )
					{
						MinDist = Dist;
						Nearest = Second;
					}
				}

				if ( !Nearest || MinDist > 725.f ) // Noone to bounce to, don't bother
					continue;

				// If Client is Enemy, we need to check if we can/should heal Nearest
				// If Client is Ally, we need to check if we can/should bounce to Nearest
				if ( const bool HealCheck = Client->IsEnemy() ? Nearest->GetHealth() + GetHealValue( Nearest, false ) <= Nearest->GetMaxHealth() : Menu::UseWOn[ Nearest->NetworkID() ]->Value; MinDist <= W.Range && HealCheck )
				{
					if ( W.CastSpell( Client ) )
					{
						LastCastTime[ 1 ] = GlobalClocks::LastUpdateNetworkTime();
						return;
					}
				}
			}
		}

		// Simple W
		//
		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client->IsDead() || !Client->IsTargetable() || Client->Distance( ObjectManager::GetPlayer() ) > W.Range )
				continue;

			const bool ToCheck = Client->IsAlly() ? Menu::HealSettings[ Client->NetworkID() ].CanHeal() : Menu::UseWOn[ Client->NetworkID() ]->Value && Orbwalker::ActiveMode == Orbwalker::Mode::Combo;

			if ( !ToCheck )
				continue;

			if ( W.CastSpell( Client ) )
			{
				LastCastTime[ 1 ] = GlobalClocks::LastUpdateNetworkTime();
				return;
			}
		}
	}

	void HandleR()
	{
		if ( !R.IsReady() || Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
			return;

		auto TargetFilter = [ ] ( AIBaseClient* Target )
		{
			return R.InRange( Target ) && !Target->mCharacterState()->IsUnstoppable();
		};

		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
		const Vector3f CursorPosition = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();

		const Prediction::Hitchance PredHitchance = GetHitchance( Menu::HitchanceR );

		if ( Menu::SemiR->Value )
		{
			const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionR )
			{
				if ( !PredictionSuccess( Status ) )
					return false;

				if ( PredictionR->Hitchance >= PredHitchance && !HasSpellShield( Target, PredictionR->TimeToTarget ) )
				{
					if ( Menu::SemiR->Value )
					{
						return CursorPosition.Distance2D( Target->Position() ) <= 500.f;
					}
				}
				return false;
			};

			auto [ BestTarget, PredictionR ] = TS->GetBestTarget( R, PlayerPosition, TargetFilter, PredFilter );

			if ( BestTarget && R.CastSpell( PredictionR.CastPosition ) )
			{
				return;
			}
		}

		if ( Menu::AmountR->Value == 0 ) // Disabled
			return;

		const std::vector<AIBaseClient*> Targets = TS->GetBestTargets( TargetFilter );

		if ( Targets.empty() )
		{
			return;
		}

		Prediction::MultiTargetPredictionResult Result;
		const auto Status = GetMultiTargetPrediction( Targets, Targets.front(), Menu::AmountR->Value, R, PlayerPosition, PlayerPosition, 0.0f, true, &Result );

		if ( !PredictionSuccess( Status ) )
		{
			return;
		}

		if ( R.CastSpell( Result.CastPosition ) )
		{
			return;
		}
	}

	void Combo()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 0 ] <= 0.4f )
			return;

		const Prediction::Hitchance MenuHitchance = GetHitchance( Menu::HitchanceQ );

		for ( const auto& [ Target, Prediction ] : TS->GetBestTargets( Q, ObjectManager::GetPlayer()->Position(), TargetFilter, PredFilter ) )
		{
			// Scripter Q Handler
			//
			const Prediction::Hitchance RequiredHitchance = ( Menu::ScripterQ->Value && IsScripter( Target ) ) ? Prediction::DashingMidAir : MenuHitchance;

			if ( Prediction.Hitchance < RequiredHitchance )
				return;

			const bool ShouldBypassRatelimiter = Prediction.Hitchance >= Prediction::VeryHigh;

			if ( Q.CastSpell( Prediction.CastPosition ) )
			{
				LastCastTime[ 0 ] = GlobalClocks::LastUpdateNetworkTime();
				return;
			}
		}
	}

#pragma endregion

#pragma region Events

	void OnProcessSpell( GameObject* Sender, Spell::SpellCastInfo* Spell )
	{
		if ( Sender && Sender->IsHero() )
		{
			if ( Sender->IsAlly() && Menu::UseESettings.contains( Sender->NetworkID() ) && E.IsReady() )
			{
				const UnitE ToCheck = Menu::UseESettings[ Sender->NetworkID() ];

				if ( Spell->HasTarget() )
				{
					GameObject* Target = Spell->GetTarget();

					if ( !Target || !Target->IsHero() )
						return;
				}

				if ( !ToCheck.IsEnabled() || !ToCheck.IsValidForE() )
					return;

				bool ShouldCast = false;

				switch ( Spell->Slot() )
				{
				case Spell::kSpellSlot::SpellSlot_Q:
					ShouldCast = ToCheck.OnQ();
					break;
				case Spell::kSpellSlot::SpellSlot_W:
					ShouldCast = ToCheck.OnW();
					break;
				case Spell::kSpellSlot::SpellSlot_E:
					ShouldCast = ToCheck.OnE();
					break;
				case Spell::kSpellSlot::SpellSlot_R:
					ShouldCast = ToCheck.OnR();
					break;
				default:
					break;
				}

				if ( ShouldCast )
				{
					if ( Menu::HumanizerE->Value <= 1 )
						return ( void )E.CastSpell( Sender );

					const uint32_t NetworkID = Sender->NetworkID();

					NamiActions.Add( [NetworkID]
					{
						if ( Orbwalker::ActiveMode != Orbwalker::Mode::Combo && Menu::OnlyComboE->Value )
							return;

						ObjectManager* Instance = ObjectManager::GetInstance();

						if ( !Instance || !E.IsReady() || GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 2 ] <= .2f )
							return;

						GameObject* Object = Instance->GetNetworkObject( NetworkID );

						if ( !Object || Object->Position().Distance2D( ObjectManager::GetPlayer()->Position() ) > E.Range )
							return;

						if ( E.CastSpell( Object ) )
						{
							LastCastTime[ 2 ] = GlobalClocks::LastUpdateNetworkTime();
						}
					}, Menu::HumanizerE->Value / 1000.f );
				}
			}
		}
	}

	void OnBasicAttack( AIBaseClient* Sender, Spell::SpellCastInfo* Spell )
	{
		if ( Sender && Sender->IsHero() && Sender->IsAlly() && Spell->GetTarget() && Spell->GetTarget()->IsHero() && Spell->GetTarget()->IsEnemy() && Menu::UseESettings.contains( Sender->NetworkID() ) && E.IsReady() )
		{
			const UnitE ToCheck = Menu::UseESettings[ Sender->NetworkID() ];
			if ( !ToCheck.IsEnabled() || !ToCheck.IsValidForE() )
				return;

			if ( ToCheck.OnAA() )
			{
				if ( Menu::HumanizerE->Value <= 1 )
					return ( void )E.CastSpell( Sender );

				const uint32_t NetworkID = Sender->NetworkID();

				NamiActions.Add( [NetworkID]
				{
					if ( Orbwalker::ActiveMode != Orbwalker::Mode::Combo && Menu::OnlyComboE->Value )
						return;

					ObjectManager* Instance = ObjectManager::GetInstance();

					if ( !Instance || !E.IsReady() || GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 3 ] <= .2f )
						return;

					GameObject* Object = Instance->GetNetworkObject( NetworkID );

					if ( !Object || Object->Position().Distance2D( ObjectManager::GetPlayer()->Position() ) > E.Range )
						return;

					if ( E.CastSpell( Object ) )
					{
						LastCastTime[ 3 ] = GlobalClocks::LastUpdateNetworkTime();
					}
				}, Menu::HumanizerE->Value / 1000.f );
			}
		}
	}

	void OnRefresh10()
	{
		CheckMenus();
	}

	void OnTick()
	{
		NamiActions.Update();
		HandleGapClose();
		HandleR();
		HandleCancels();

		switch ( Orbwalker::ActiveMode )
		{
		case Orbwalker::Mode::Combo:
			Combo();
			break;
		default:
			break;
		}

		HandleW();
	}

#pragma endregion

	void Initialize()
	{
		auto BubbleTravelTime = [ ] ( const BaseSpell& Spell, const float Distance ) -> float
		{
			return Spell.CastDelay + .726f;
		};

		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.Type = Circular,
			.CastDelay = 0.25f,
			.Range = 850.f,
			.RangeType = Center,
			.Speed = 0.f,
			.CalculateTimeTraveledCustom = BubbleTravelTime,
			.Radius = 200.f,
			.HeroCollisionType = SpellCollisionType::None,
			.MinionCollisionType = SpellCollisionType::None,
			.TowerCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Targeted,
			.CastDelay = 0.25f,
			.Range = 725.f,
			.RangeType = Center,
			.Speed = 2500.f,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		BounceW = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Targeted,
			.CastDelay = 0.f,
			.Range = 725.f,
			.RangeType = Center,
			.Speed = 2500.f,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Targeted,
			.CastDelay = 0.f,
			.Range = 800.f
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.Type = Linear,
			.CastDelay = 0.5f,
			.Range = 2750.f,
			.RangeType = Center,
			.Speed = 850.f,
			.Radius = 250.f,
			.HeroCollisionType = Soft,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueNami" ), xorstr_( "Blue Nami" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AP );

		const auto MenuQ = Main->AddMenu( xorstr_( "MenuQ" ), xorstr_( "[Q / Aqua Prison] Settings" ) );
		MenuQ->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );

		Menu::ComboQ = MenuQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
		Menu::SlowQ = MenuQ->AddCheckBox( xorstr_( "SlowQ" ), xorstr_( "Only if Target is Slowed" ), false );

		const auto UseQMenu = MenuQ->AddMenu( xorstr_( "UseQOn" ), xorstr_( "Use on..." ) );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Client ) )
				continue;

			const char* DisplayName = GetDisplayName( Client );

			Menu::UseQOn[ Client->NetworkID() ] = UseQMenu->AddCheckBox( std::to_string( static_cast<unsigned int>( Client->SkinHash() ) ).c_str(), DisplayName, true );
		}

		MenuQ->AddInfo( xorstr_( "Spacer" ), xorstr_( "" ) );
		Menu::GapCloseQ = MenuQ->AddCheckBox( xorstr_( "gapQ" ), xorstr_( "Anti Gap Closers" ), true );
		Menu::CancelQ = MenuQ->AddCheckBox( xorstr_( "cancelQ" ), xorstr_( "Use to Cancel Spells" ), true );
		Menu::CancelQImportance = MenuQ->AddSlider( xorstr_( "cancelQImp" ), xorstr_( " ^- If Casting Importance >= " ), 1, 1, 3, 1 );
		Menu::HitchanceQ = MenuQ->AddList( xorstr_( "HitchanceQ" ), xorstr_( "Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 4 );
		Menu::ScripterQ = MenuQ->AddCheckBox( xorstr_( "ScripterQ" ), xorstr_( "Only Guaranteed hit on Scripters" ), true );

		const auto MenuW = Main->AddMenu( xorstr_( "MenuW" ), xorstr_( "[W / Ebb and Flow] Settings" ) );
		MenuW->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_W )->GetIcon() );

		Menu::ComboW = MenuW->AddCheckBox( xorstr_( "ComboW" ), xorstr_( "Use in Combo" ), true );
		Menu::BounceW = MenuW->AddCheckBox( xorstr_( "BounceW" ), xorstr_( "Allow Bounce Logic" ), true );

		const auto Heal = MenuW->AddMenu( xorstr_( "healMenu" ), xorstr_( "Heal Settings" ) );
		Menu::HealW = Heal->AddCheckBox( xorstr_( "enableHealW" ), xorstr_( "Enable [Heal]" ), true );
		Menu::HealMana = Heal->AddSlider( xorstr_( "healMana" ), xorstr_( "Only if Mana >= #%" ), 60, 0, 100, 5 );

		const auto HealChamps = Heal->AddMenu( xorstr_( "useHealOn" ), xorstr_( "Use [Heal] on:" ) );

		int i = 0;

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Client, true ) )
				continue;

			i++;

			if ( i > 1 )
				HealChamps->AddInfo( ( xorstr_( "separator" ) + std::to_string( i ) ).c_str(), xorstr_( "---" ) );

			HealUnit Temporary;

			std::string DisplayName = GetDisplayName( Client );
			std::string CharName = Client->SkinName();

			Temporary.Client = Client;
			Temporary.HealToggle = HealChamps->AddCheckBox( CharName.c_str(), ( xorstr_( "[" ) + DisplayName + xorstr_( "]" ) ).c_str(), true );
			Temporary.HealToggle->AddTexture( Client->GetIconSquare() );
			Temporary.HealPercentage = HealChamps->AddSlider( ( CharName + "HP" ).c_str(), xorstr_( "Only if HP < #%" ), 30, 0, 100, 5 );

			Menu::HealSettings[ Client->NetworkID() ] = Temporary;
		}

		const auto UseWMenu = MenuW->AddMenu( xorstr_( "UseWOn" ), xorstr_( "Use on..." ) );

		UseWMenu->AddInfo( xorstr_( "AlliesSep" ), xorstr_( "Allies" ) );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Client, true ) )
				continue;

			std::string DisplayName = GetDisplayName( Client );
			Menu::UseWOn[ Client->NetworkID() ] = UseWMenu->AddCheckBox( ( std::string( Client->SkinName() ) + "A" ).c_str(), ( xorstr_( "[" ) + DisplayName + xorstr_( "]" ) ).c_str(), !Client->IsMe() );
		}

		UseWMenu->AddInfo( xorstr_( "AlliesEne" ), xorstr_( "Enemies" ) );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Client ) )
				continue;

			std::string DisplayName = GetDisplayName( Client );
			Menu::UseWOn[ Client->NetworkID() ] = UseWMenu->AddCheckBox( ( std::string( Client->SkinName() ) + "E" ).c_str(), ( xorstr_( "[" ) + DisplayName + xorstr_( "]" ) ).c_str(), true );
		}

		const auto MenuE = Main->AddMenu( xorstr_( "MenuE" ), xorstr_( "[E / Tidecaller's Blessing] Settings" ) );
		MenuE->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );

		Menu::UseE = MenuE->AddCheckBox( xorstr_( "UseE" ), xorstr_( "Toggle E Usage" ), true );
		Menu::OnlyComboE = MenuE->AddCheckBox( xorstr_( "ComboE" ), xorstr_( " ^- Only in Combo" ), true );
		Menu::HumanizerE = MenuE->AddSliderF( xorstr_( "HumanizerE" ), xorstr_( "Humanizer (in ms)" ), 250.f, 0.f, 800.f, 50.f );

		const auto ESettings = MenuE->AddMenu( xorstr_( "eSettings" ), xorstr_( "Advanced Settings" ) );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Client, true ) )
				continue;

			UnitE Temporary{};
			std::string DisplayName = GetDisplayName( Client );
			std::string CharName = Client->SkinName();

			Temporary.Menu = ESettings->AddMenu( CharName.c_str(), ( xorstr_( "[" ) + DisplayName + xorstr_( "]" ) ).c_str() );
			Temporary.Menu->AddTexture( Client->GetIconSquare() );

			Temporary.Booleans[ 0 ] = Temporary.Menu->AddCheckBox( xorstr_( "Enable" ), ( xorstr_( "Enable [" ) + DisplayName + xorstr_( "]" ) ).c_str(), true );
			Temporary.Priority = Temporary.Menu->AddSlider( xorstr_( "Priority" ), xorstr_( "Priority" ), 3, 1, 5, 1 );
			Temporary.Menu->AddInfo( xorstr_( "separator" ), xorstr_( " --- " ) );

			Temporary.Booleans[ 5 ] = Temporary.Menu->AddCheckBox( xorstr_( "AA" ), xorstr_( "Use on [AA]" ), true );
			Temporary.Menu->AddInfo( xorstr_( "separator2" ), xorstr_( " - " ) );
			Temporary.Booleans[ 1 ] = Temporary.Menu->AddCheckBox( xorstr_( "Q" ), xorstr_( "Use on [Q]" ), false );
			Temporary.Booleans[ 2 ] = Temporary.Menu->AddCheckBox( xorstr_( "W" ), xorstr_( "Use on [W]" ), false );
			Temporary.Booleans[ 3 ] = Temporary.Menu->AddCheckBox( xorstr_( "E" ), xorstr_( "Use on [E]" ), false );
			Temporary.Booleans[ 4 ] = Temporary.Menu->AddCheckBox( xorstr_( "R" ), xorstr_( "Use on [R]" ), true );

			Temporary.Unit = Client;

			Menu::UseESettings.emplace( Client->NetworkID(), Temporary );
		}

		ESettings->AddInfo( xorstr_( "targetedInfo" ), xorstr_( "All Targeted Spells (including AAs)" ) );
		ESettings->AddInfo( xorstr_( "targetedInfo2" ), xorstr_( "Will _ONLY_ trigger if they target Champions." ) );

		const auto MenuR = Main->AddMenu( xorstr_( "MenuR" ), xorstr_( "[R / Tidal Wave] Settings" ) );
		MenuR->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->GetIcon() );

		Menu::AmountR = MenuR->AddSlider( xorstr_( "AmountR" ), xorstr_( "Minimum Hits of [R]" ), 3, 0, 6, 1 );
		Menu::SemiR = MenuR->AddKeyBind( xorstr_( "SemiR" ), xorstr_( "Semi [R]" ), 'T', false, false );
		MenuSDK::PermaShow::AddComponent( xorstr_( "Semi R" ), Menu::SemiR );

		MenuR->AddInfo( xorstr_( "Spacer" ), xorstr_( "" ) );
		Menu::CancelR = MenuR->AddCheckBox( xorstr_( "cancelR" ), xorstr_( "Use to Cancel Spells" ), true );
		Menu::CancelRImportance = MenuR->AddSlider( xorstr_( "cancelRImp" ), xorstr_( " ^- If Casting Importance >= " ), 3, 1, 3, 1 );
		Menu::HitchanceR = MenuR->AddList( xorstr_( "hitchanceR" ), xorstr_( "Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 2 );

		const auto CancelMenu = Main->AddMenu( xorstr_( "cancelMenu" ), xorstr_( "Spells to Cancel:" ) );
		InitializeCancelMenu( CancelMenu );

		EventHandler::Add( Event::OnProcessSpell, OnProcessSpell );
		EventHandler::Add( Event::OnBasicAttack, OnBasicAttack );
		EventHandler::Add( Event::OnGameUpdate, OnTick );
		EventHandler::Add( Event::OnRefresh10, OnRefresh10 );
	}
}
