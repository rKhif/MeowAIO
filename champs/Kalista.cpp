#include "Kalista.h"
#include "UtilityHUDHP.h"

namespace Features::Champions::Kalista
{
	BaseSpell Q, W, E, R;

	TargetSelector::Instance* TS;

	constexpr float QBase[ 6 ] = { 0.f, 20.f, 85.f, 150.f, 215.f, 280.f };
	constexpr float QRatio = 1.f;
	constexpr float QCost[ 6 ] = { 0.f, 50.f, 60.f, 65.f, 70.f };

	constexpr float EBaseFirst[ 6 ] = { 0.f, 20.f, 30.f, 40.f, 50.f, 60.f };
	constexpr float EFirstRatioAD = .7f;
	constexpr float EFirstRatioAP = .2f;
	constexpr float EBaseAdditional[ 6 ] = { 0.f, 8.f, 12.f, 16.f, 20.f, 24.f };
	constexpr float EAdditionalRatioAD[ 6 ] = { 0.f, .25f, .3f, .35f, .4f, .45f };
	constexpr float EAdditionalRatioAP = .2f;
	constexpr float ECost = 30.f;
	constexpr float ERefund[ 6 ] = { 0.f, 10.f, 15.f, 20.f, 25.f, 30.f };

	AIBaseClient* Bound = nullptr;
	float LastBoundCheck = -5;
	float LastAttackTime = -30.f;

#pragma region Helpers

	bool CanAttack()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() - LastAttackTime < ObjectManager::GetPlayer()->ComputeCharacterAttackCastDelay() + ObjectManager::GetPlayer()->ComputeCharacterAttackDelay() )
			return false;

		return true;
	}

	float GetQDamage( AIBaseClient* Target )
	{
		return Damage::CalculatePhysicalDamage( ObjectManager::GetPlayer(), Target, QBase[ Q.Level() ] + QRatio * ObjectManager::GetPlayer()->TotalAttackDamage() );
	}

	float GetEDamage( AIBaseClient* Target, const int AdditionalStacks = 0 )
	{
		const uint32_t Level = E.Level();
		float Damage = 0;
		if ( Spell::Buff::BuffInstance* Instance = Target->BuffManager()->FindBuff( "kalistaexpungemarker" ) )
		{
			Damage += EBaseFirst[ Level ] + EFirstRatioAD * ObjectManager::GetPlayer()->TotalAttackDamage() + EFirstRatioAP * ObjectManager::GetPlayer()->TotalAbilityPower();

			if ( const int32_t Stacks = static_cast<int32_t>( Instance->GetStacks() ) + AdditionalStacks - 1; Stacks > 0 )
			{
				Damage += Stacks * ( EBaseAdditional[ Level ] + EAdditionalRatioAD[ Level ] * ObjectManager::GetPlayer()->TotalAttackDamage() + EAdditionalRatioAP * ObjectManager::GetPlayer()->TotalAbilityPower() );
				Damage = Damage::CalculatePhysicalDamage( ObjectManager::GetPlayer(), Target, Damage );
				return Target->IsEpicMonster() ? Damage / 2 : Damage;
			}
		}

		Damage = Damage::CalculatePhysicalDamage( ObjectManager::GetPlayer(), Target, Damage );
		return Target->IsEpicMonster() ? Damage / 2 : Damage;
	}

	void KillableE()
	{
		if ( E.IsReady() )
		{
			for ( const auto& Entity : ManagerTemplates::Heroes() )
			{
				if ( !Entity->IsDead() && GetEDamage( Entity ) >= Entity->GetRealHealth( true, false ) && Entity->IsValidTarget( E.Range, true, ObjectManager::GetPlayer()->Position() ) && E.CastSpell() )
				{
					return;
				}
			}
		}
	}

	bool ShouldJump()
	{
		AIHeroClient* Player = ObjectManager::GetPlayer();

		const float PlayerMS = Player->CharacterIntermediate()->MoveSpeed();
		const float TimeRequiredToAA = Player->ComputeCharacterAttackCastDelay() + MultiplayerClient::GetInstance()->CurrentRoundTripTime() * 1.2f;
		const float WalkRange = PlayerMS * TimeRequiredToAA;

		float JumpRange = 250.f; // With no boots

		// Finished Boots
		if ( Player->HasItems( { Item::BerserkersGreaves, Item::MobilityBoots, Item::PlatedSteelcaps, Item::BootsofSwiftness, Item::MercurysTreads, Item::IonianBootsofLucidity, Item::SorcerersShoes } ) != Item::INVALID )
		{
			JumpRange = 265.f;
		} // Basic Boots
		else if ( Player->HasItems( { Item::Boots, Item::SlightlyMagicalFootwear } ) != Item::INVALID )
		{
			JumpRange = 280.f;
		}

		return JumpRange > WalkRange;
	}

	namespace sQ
	{
		uint32_t EnemiesInAARange = 0;

		const std::function TargetFilter = [&] ( AIBaseClient* Target )
		{
			if ( Orbwalker::SDK::GetAttackSpeed( ObjectManager::GetPlayer() ) > Menu::BlockQ->Value )
				return false;

			return true;
		};

		const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* Prediction )
		{
			if ( !PredictionSuccess( Status ) )
				return false;

			if ( Prediction->Hitchance < Prediction::Medium )
				return false;

			for ( const auto& Object : Prediction->CollidedObjects )
			{
				auto AIBase = Object.Unit->AsAIBase();
				if ( !AIBase )
					continue;

				const float DamageQ = GetQDamage( AIBase );

				// Maybe not needed
				//
				if ( Object.Unit->NetworkID() == Target->NetworkID() )
					continue;

				if ( Object.Unit->GetHealth() > DamageQ )
					return false;
			}

			// KS Q
			//
			// if (Orbwalker::ActiveMode != Orbwalker::Mode::Combo && Target->GetRealHealth(true, false) > GetQDamage(Target) )
			//		return false;

			return true;
		};
	}

	void SlowE()
	{
		if ( !E.IsReady() || ObjectManager::GetPlayer()->mResource()->mPAR.Value < ECost * 2.f )
			return;

		if ( !Menu::SlowE->Value )
			return;

		int EnemyCount = 0;

		for ( const auto& EnemyHero : ManagerTemplates::Heroes() )
		{
			if ( !EnemyHero->IsDead() && EnemyHero->IsEnemy() && EnemyHero->BuffManager()->HasBuff( HASH( "kalistaexpungemarker" ) ) && EnemyHero->Distance( ObjectManager::GetPlayer() ) <= E.Range )
			{
				EnemyCount++;
				break;
			}
		}

		if ( EnemyCount == 0 )
			return;

		for ( const auto& EnemyMinion : ManagerTemplates::Minions() )
		{
			if ( !EnemyMinion->IsDead() && EnemyMinion->IsEnemy() && EnemyMinion->BuffManager()->HasBuff( HASH( "kalistaexpungemarker" ) ) && GetEDamage( EnemyMinion ) >= EnemyMinion->GetRealHealth( true, false ) && E.CastSpell() )
			{
				return;
			}
		}
	}

	bool HandleQ( AIBaseClient* Client, const Prediction::PredictionResult& Prediction )
	{
		const float CurrentMana = ObjectManager::GetPlayer()->mResource()->mPAR.Value;

		if ( CurrentMana < QCost[ Q.Level() ] + ECost )
			return false;

		if ( Menu::ModeQ->Values[ 0 ] ) // Always
		{
			if ( Q.CastSpell( Prediction.CastPosition ) )
				return true;
		}

		if ( Menu::ModeQ->Values[ 1 ] ) // After Attack
		{
			if ( !CanAttack() && Q.CastSpell( Prediction.CastPosition ) )
				return true;
		}

		if ( Menu::ModeQ->Values[ 2 ] ) // Out of Range
		{
			if ( sQ::EnemiesInAARange == 0 && Q.CastSpell( Prediction.CastPosition ) )
				return true;
		}

		return false;
	}

	void Combo()
	{
		SlowE();

		if ( Menu::ComboE->Value )
			KillableE();

		sQ::EnemiesInAARange = EnemyHeroTargetsInRange( ObjectManager::GetPlayer(), pow( ObjectManager::GetPlayer()->GetAutoAttackRange(), 2 ) );

		if ( Q.IsReady() )
		{
			auto [ Target, Prediction ] = TS->GetBestTarget( Q, ObjectManager::GetPlayer()->Position(), sQ::TargetFilter, sQ::PredFilter );

			if ( Target && HandleQ( Target, Prediction ) )
			{
				Orbwalker::SDK::OrbwalkTo( HudManager::GetInstance()->CursorTargetLogic()->CursorPosition() );
				return;
			}
		}

		// Orb minions
		if ( sQ::EnemiesInAARange == 0 && Menu::OrbMinions->Value )
		{
			if ( Menu::RestrictOrb->Value && !ShouldJump() )
				return;

			for ( const auto& Minion : ManagerTemplates::Minions() )
			{
				if ( Minion->IsValidTarget( ObjectManager::GetPlayer()->GetAutoAttackRange(), true, ObjectManager::GetPlayer()->Position() ) )
				{
					Orbwalker::SDK::OrbwalkTo( HudManager::GetInstance()->CursorTargetLogic()->CursorPosition(), Minion );
				}
			}
		}
	}

	void Clear()
	{
		int AmountOfMinions = 0;
		for ( const auto& Minion : ManagerTemplates::Minions() )
		{
			if ( Minion->IsDead() || Minion->IsAlly() || !Minion->BuffManager()->HasBuff( HASH( "kalistaexpungemarker" ) ) )
				continue;

			if ( Minion->IsEpicMonster() && Menu::JungleClearE->Values[ 0 ] && GetEDamage( Minion ) >= Minion->GetRealHealth() && E.CastSpell() )
				return;

			if ( Minion->IsLargeMonster() && Menu::JungleClearE->Values[ 1 ] && GetEDamage( Minion ) >= Minion->GetRealHealth() && E.CastSpell() )
				return;

			if ( !Minion->IsLaneMinion() && !Minion->IsLargeMonster() && Menu::JungleClearE->Values[ 2 ] && GetEDamage( Minion ) >= Minion->GetRealHealth() && E.CastSpell() ) // Small Minion doesn't exist
				return;

			if ( Minion->IsLaneMinion() && GetEDamage( Minion ) >= Minion->GetRealHealth() )
				AmountOfMinions++;

			if ( Menu::LaneClearE->Value && AmountOfMinions >= Menu::LaneMinionsAmount->Value && E.CastSpell() )
				return;
		}
	}

	AIBaseClient* FindBound()
	{
		for ( const auto& Entity : ManagerTemplates::Heroes() )
		{
			if ( Entity->IsAlly() && Entity->BuffManager()->HasBuff( HASH( "kalistacoopstrikeally" ) ) )
			{
				return Entity;
			}
		}
		return nullptr;
	}

#pragma endregion Helpers

#pragma region Events

	void OnDraw()
	{
		if ( Menu::DamageDrawMode->Value == 2 )
			return;

		for ( const auto& Client : ManagerTemplates::AIBases() )
		{
			if ( Client->IsDead() || Client->IsAlly() || !Client->IsOnScreen() || !Client->IsVisible() || !Client->ShouldDrawHealthBar() || !Client->BuffManager()->HasBuff( HASH( "kalistaexpungemarker" ) ) )
				continue;

			const Vector2f BarSize = GetHealthbarSize( Client );
			const Vector2f BarPosition = GetHealthbarPosition( Client );

			const float Damage = GetEDamage( Client );

			std::string ToWrite;

			if ( Menu::DamageDrawMode->Value == 0 )
			{
				ToWrite = "[" + std::to_string( static_cast<int>( Damage ) ) + "]";
			}
			else
			{
				ToWrite = "[" + std::to_string( static_cast<int>( std::floorf( Damage / Client->GetRealHealth( true ) * 100.f ) ) ) + "%]";
			}
			const Vector2f TextSize = Graphics::TextSize( ToWrite.c_str(), max( floor((BarSize.y * 2.3f)), 24 ), true );
			Vector2f WritePosition = { BarPosition.x + BarSize.x + 4, BarPosition.y + BarSize.y / 2 - TextSize.y / 2 + 2.f };

			if ( Utility::HUDHP::TrackSummoners->Value && Client->IsHero() )
				WritePosition.x += Utility::HUDHP::HUDRadius + 3.f;

			const DWORD Color = Damage > Client->GetRealHealth() ? Graphics::RGBA( 215, 0, 64, 255 ) : Graphics::RGBA( 93, 63, 211, 255 );

			Graphics::AddTextStroke( ToWrite.c_str(), max( floor((BarSize.y * 2.3f)), 24 ), WritePosition, Damage > Client->GetRealHealth() ? Graphics::RGBA( 215, 0, 64, 255 ) : Graphics::RGBA( 255, 255, 255, 255 ), true );
			DrawHealthbarDamage( Client, Damage, Color );
		}
	}

	void OnDrawWorld()
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		if ( Menu::DrawQ->Value && Q.IsReady() )
			Graphics::AddCircle( PlayerPosition, Q.Range, 1, Menu::ColorQ->Value );

		if ( Menu::DrawE->Value && E.IsReady() )
			Graphics::AddCircle( PlayerPosition, E.Range, 1, Menu::ColorE->Value );
	}

	void OnTick()
	{
		if ( !Bound && GlobalClocks::LastUpdateNetworkTime() - LastBoundCheck >= 3.5f )
		{
			Bound = FindBound();
			LastBoundCheck = GlobalClocks::LastUpdateNetworkTime();
		}

		if ( Menu::KillstealE->Value )
			KillableE();

		switch ( Orbwalker::ActiveMode )
		{
		case Orbwalker::Mode::Combo:
			Combo();
			break;
		case Orbwalker::Mode::Laneclear:
			Clear();
			break;
		default:
			break;
		}
	}

	void OnOrbwalkerAction( const Orbwalker::ActionArgs& Args, bool* Process )
	{
		if ( !Args.Target || !Args.Target->IsHero() || Args.Type != Orbwalker::ActionArgs::After || Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
			return;

		if ( Orbwalker::SDK::GetAttackSpeed( ObjectManager::GetPlayer() ) > Menu::BlockQ->Value )
			return;

		AIBaseClient* Client = Args.Target->AsAIBase();

		const float CurrentMana = ObjectManager::GetPlayer()->mResource()->mPAR.Value;
		if ( CurrentMana < QCost[ Q.Level() ] + ECost )
			return;

		if ( !Client )
			return;

		if ( Q.IsReady() && Menu::ModeQ->Values[ 1 ] )
		{
			Prediction::PredictionResult PredQ;
			if ( PredictionSuccess( GetPrediction(Client, Q, ObjectManager::GetPlayer()->PositionOnPath(), ObjectManager::GetPlayer()->PositionOnPath(), true, &PredQ) ) )
			{
				if ( PredQ.Hitchance >= Prediction::Medium && Q.CastSpell( PredQ.CastPosition ) )
				{
					Orbwalker::SDK::OrbwalkTo( HudManager::GetInstance()->CursorTargetLogic()->CursorPosition() );
					return;
				}
			}
		}
	}

	void OnBasicAttack( Riot::AIBaseClient* Sender, Spell::SpellCastInfo* CastInfo )
	{
		if ( !Sender || !Sender->IsMe() )
			return;

		LastAttackTime = GlobalClocks::LastUpdateNetworkTime();
	}

#pragma endregion Events

	void Initialize()
	{
		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 1200.f,
			.RangeType = Center,
			.Speed = 2400.f,
			.Radius = 40.f,
			.HeroCollisionType = Soft,
			.MinionCollisionType = Soft,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Circular,
			.CastDelay = .25f,
			.Range = 5000.f,
			.RangeType = Center,
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Self,
			.CastDelay = .25f,
			.Range = 1100.f,
			.RangeType = Center,
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.Type = Self,
			.CastDelay = .0f,
			.Range = 1200.f,
			.RangeType = Center,
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueKalista" ), xorstr_( "Blue Kalista" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AA );

		const auto MenuP = Main->AddMenu( xorstr_( "MenuP" ), xorstr_( "[P / Martial Poise] Settings" ) );
		MenuP->AddTexture( ObjectManager::GetPlayer()->GetPassiveIcon() );
		Menu::OrbMinions = MenuP->AddCheckBox( xorstr_( "OrbMinions" ), xorstr_( "Orbwalk on Minions/etc. if out of Range" ), true );
		Menu::RestrictOrb = MenuP->AddCheckBox( xorstr_( "RestrictOrb" ), xorstr_( " ^- Only if Jump covers a bigger distance than walking" ), true );

		const auto MenuQ = Main->AddMenu( xorstr_( "MenuQ" ), xorstr_( "[Q / Pierce] Settings" ) );
		MenuQ->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );
		Menu::ComboQ = MenuQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
		Menu::ModeQ = MenuQ->AddMultiList( xorstr_( "ModeQ" ), xorstr_( "Usages" ), { xorstr_( "Always" ), xorstr_( "On After Attack" ), xorstr_( "Out of Range" ) }, { 1 } );
		Menu::BlockQ = MenuQ->AddSliderF( xorstr_( "BlockQ" ), xorstr_( "Block if Attack Speed >= #" ), 3.5f, 1.0f, 5.0f, 0.25f );
		Menu::DrawQ = MenuQ->AddCheckBox( xorstr_( "DrawQ" ), xorstr_( "Draw Q Range" ), false );
		Menu::ColorQ = MenuQ->AddColorPicker( xorstr_( "ColorQ" ), xorstr_( "Range Color" ), Graphics::RGBA( 8, 143, 143, 122 ) );

		const auto MenuE = Main->AddMenu( xorstr_( "MenuE" ), xorstr_( "[E / Rend] Settings" ) );
		MenuE->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );
		Menu::ComboE = MenuE->AddCheckBox( xorstr_( "ComboE" ), xorstr_( "Use in Combo" ), true );
		Menu::AllowAfterAAE = MenuE->AddCheckBox( xorstr_( "AllowAfterAAE" ), xorstr_( "Allow [E] / [Rend] After Attack" ), false );
		Menu::SlowE = MenuE->AddCheckBox( xorstr_( "SlowE" ), xorstr_( "Use [E] / [Rend] to Slow Enemy if CD resets" ), true );
		Menu::KillstealE = MenuE->AddCheckBox( xorstr_( "KillStealE" ), xorstr_( "Killsteal" ), true );
		Menu::DamageDrawMode = MenuE->AddList( xorstr_( "DamageDrawMode" ), xorstr_( "Damage Draw" ), { xorstr_( "Damage" ), xorstr_( "Percentage" ), xorstr_( "Disabled" ) }, 0 );
		Menu::DrawE = MenuE->AddCheckBox( xorstr_( "DrawE" ), xorstr_( "Draw E Range" ), false );
		Menu::ColorE = MenuE->AddColorPicker( xorstr_( "ColorE" ), xorstr_( "Range Color" ), Graphics::RGBA( 100, 149, 237, 122 ) );

		MenuE->AddInfo( xorstr_( "ClearSpacer" ), xorstr_( "Clear Settings" ) );
		Menu::LaneClearE = MenuE->AddCheckBox( xorstr_( "LaneE" ), xorstr_( "Use [E] / [Rend] for Minions" ), false );
		Menu::LaneMinionsAmount = MenuE->AddSlider( xorstr_( "AmountE" ), xorstr_( "Amount of Minions Required:" ), 3, 1, 7 );
		Menu::JungleClearE = MenuE->AddMultiList( xorstr_( "jungleE" ), xorstr_( "Use on Jungle Mobs:" ), { xorstr_( "Epic" ), xorstr_( "Large" ), xorstr_( "Small" ) }, { 0, 1 } );

		EventHandler::Add( Event::OnOrbwalkerAction, OnOrbwalkerAction );
		EventHandler::Add( Event::OnGameUpdate, OnTick );
		EventHandler::Add( Event::Present, OnDraw );
		EventHandler::Add( Event::OnBasicAttack, OnBasicAttack );
		EventHandler::Add( Event::WorldDraw, OnDrawWorld );
	}

	void Destroy()
	{
	}
}
