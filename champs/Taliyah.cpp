#include "Taliyah.h"

#include <ranges>


namespace Features::Champions::Taliyah
{
	bool IsBoulder = false;
	float DynamicDistance = 0.f;
	float LastJungleMines = 0.f;

	enum Direction
	{
		AWAY,
		TOWARDS
	};

	bool CanUseOnCamp( const BaseSpell& Spell, AIBaseClient* Client )
	{
		const MenuSDK::MultiList* Entry;

		switch ( Spell.Slot )
		{
		case Spell::kSpellSlot::SpellSlot_Q:
			Entry = Menu::JungleQ;
			break;
		case Spell::kSpellSlot::SpellSlot_W:
			Entry = Menu::JungleW;
			break;
		case Spell::kSpellSlot::SpellSlot_E:
			Entry = Menu::JungleE;
			break;
		default:
			return false;
		}

		const int LocalValue = Client->IsEpicMonster() ? 0 : ( Client->IsLargeMonster() ? 1 : ( Client->IsSmallMonster() ? 2 : 4 ) );

		if ( LocalValue == 4 || !Entry )
			return false;

		return Entry->Values[ LocalValue ];
	}

	void PreCastE();

	Direction CurrentDirection = AWAY;
	AIBaseClient* VolleyTarget = nullptr;
	float MagnetUntil = -1.f;
	Vector3f VolleyDirection = Vector3f::Zero;
	float LastMagnetMove = 0.f;

	struct ShoveTarget
	{
		float LastChange = -1.f;
		AIBaseClient* Unit = nullptr;
		Vector3f CastPosition;
		Vector3f Direction;
	};

	ShoveTarget CurrentW = {};

	float LastRide = 0.f;

	Vector3f Temp = Vector3f::Zero;

	void UpdateMenu( ... )
	{
		CurrentDirection = CurrentDirection == AWAY ? TOWARDS : AWAY;
	}

	constexpr float TaliyahR[ 4 ] = { 0.f, 2500.f, 4500.f, 6500.f };

	void UpdateSpells()
	{
		R.Range = TaliyahR[ R.Level() ];
	}

	float GetDynamicDistance( AIBaseClient* Target )
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
		const Vector3f CursorPosition = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();
		const Vector3f TargetPosition = Target->Position();

		// Cursor is relatively close to Player, assume distance
		//
		if ( CursorPosition.Distance2D( PlayerPosition ) <= 100.f )
			return 1337.f;

		if ( TargetPosition.Distance2D( CursorPosition ) <= TargetPosition.Distance2D( PlayerPosition ) )
			DynamicDistance -= 25.f;
		else
			DynamicDistance += 25.f;

		DynamicDistance = max( DynamicDistance, 500 );
		DynamicDistance = min( 950.f, DynamicDistance );

		return DynamicDistance;
	}

	void MagnetQ()
	{
		if ( !VolleyTarget || !VolleyTarget->IsVisible() || VolleyTarget->IsDead() || GlobalClocks::LastUpdateNetworkTime() > MagnetUntil )
			return;

		// None
		//
		if ( Menu::MagnetQ->Value == 2 )
			return;

		// Soft
		//
		if ( Menu::MagnetQ->Value == 1 && GlobalClocks::LastUpdateNetworkTime() - LastMagnetMove <= .33f )
			return;

		// Hard
		//
		if ( Menu::MagnetQ->Value == 0 && GlobalClocks::LastUpdateNetworkTime() - LastMagnetMove <= .1f )
			return;

		Orbwalker::SDK::SetAttackPause( MagnetUntil - GlobalClocks::LastUpdateNetworkTime() );

		Prediction::PredictionData PredQ;
		GetPositionAfterTime( VolleyTarget, 0.1f, &PredQ );

		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		const float DesiredDistance = GetDynamicDistance( VolleyTarget );

		// For some reason this is needed otherwise the distance is incorrect
		//
		Vector3f DummyDirection = PredQ.Position - VolleyDirection;
		DummyDirection.y = PlayerPosition.y;
		Vector3f TempPosition = PredQ.Position;
		TempPosition.y = PlayerPosition.y;

		const Vector3f MagnetPosition = TempPosition.Extend( DummyDirection, DesiredDistance );

		Orbwalker::SDK::SetMovePause( -1.f );

		if ( DesiredDistance <= 1336.f )
			Orbwalker::SDK::OrbwalkTo( MagnetPosition );
		else
			Orbwalker::SDK::OrbwalkTo( PlayerPosition );

		LastMagnetMove = GlobalClocks::LastUpdateNetworkTime();
		Orbwalker::SDK::SetMovePause( 0.15f );
	}

	namespace sQ
	{
		const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionQ )
		{
			return Status == Prediction::Success || Status == Prediction::Collision;
		};

		void Combo()
		{
			if ( !Q->IsReady() || !Menu::ComboQ->Value )
				return;

			auto [ Target, Prediction ] = TS->GetBestTarget( *Q, ObjectManager::GetPlayer()->Position(), nullptr, PredFilter );
			if ( Target )
			{
				if ( Prediction.Hitchance >= Prediction::High )
				{
					( void )Q->CastSpell( Prediction.CastPosition );
					VolleyTarget = Target;
					return;
				}

				if ( Menu::AOEQ->Value )
				{
					if (Prediction.CollidedObjects.empty())
						return;

					const uint32_t TargetID = Target->NetworkID();
					const Prediction::CollidedObject CollidedUnit = Prediction.CollidedObjects.front();
					Prediction::CollidedObject TargetProxy = { nullptr, Vector3f::Zero };

					for (const auto& Unit : Prediction.CollidedObjects)
					{
						if (Unit.Unit && Unit.Unit->NetworkID() == TargetID)
						{
							TargetProxy = Unit;
							break;
						}
					}

					if (TargetProxy.Unit && CollidedUnit.Unit->Position().Distance2D(TargetProxy.Position) <= 225.f)
					{
						(void)Q->CastSpell(Prediction.CastPosition);
						VolleyTarget = Target;
					}
				}
			}
		}

		void Clear()
		{
			if ( !Q->IsReady() )
				return;

			AIBaseClient* BestTarget = nullptr;
			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
			Prediction::PredictionResult BestResult;

			for ( const auto& Client : ManagerTemplates::Minions() )
			{
				if ( !Client || Client->IsDead() || !Client->IsValidTarget( Q->Range, true, PlayerPosition ) || Client->IsLaneMinion() )
					continue;

				if ( !CanUseOnCamp( *Q, Client ) )
					continue;

				Prediction::PredictionResult PredQ;
				if ( !PredictionSuccess( GetPrediction(Client, *Q, PlayerPosition, PlayerPosition, true, &PredQ) ) )
					continue;

				if ( Client->GetHealth() < 200.f && !IsBoulder )
					continue;

				if ( !BestTarget || BestTarget->GetMaxHealth() < Client->GetMaxHealth() )
				{
					BestTarget = Client;
					BestResult = PredQ;
				}
			}

			if ( BestTarget )
				( void )Q->CastSpell( BestResult.CastPosition );
		}
	}

	namespace sW
	{
		const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* PredictionW )
		{
			return !HasSpellShield( Target, PredictionW->TimeToTarget ) && PredictionSuccess( Status );
		};

		void Combo()
		{
			if ( !W.IsReady() || !E.IsReady() || !Menu::ComboW->Value )
				return;

			const float CurrentMana = ObjectManager::GetPlayer()->mResource()->mPAR.Value;

			if ( CurrentMana <= CostW[ W.Level() ] + CostE[ E.Level() ] )
				return;

			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
			auto [ Target, Prediction ] = TS->GetBestTarget( W, ObjectManager::GetPlayer()->Position(), nullptr, PredFilter );
			if ( Target && Prediction.Hitchance >= Prediction::High )
			{
				// We assume towards us
				//
				Vector3f Direction = Prediction.CastPosition.Extend( PlayerPosition, 10.f );

				if ( Prediction.CastPosition.Distance2D( PlayerPosition ) <= W.Range / 2.f )
					Direction = Prediction.CastPosition.Extend( PlayerPosition, -10.f );

				( void )W.CastSpell( Prediction.CastPosition, Direction );
				CurrentW.Unit = Target;
				CurrentW.LastChange = GlobalClocks::LastUpdateNetworkTime();
				CurrentW.Direction = ( Direction - Prediction.CastPosition ).Normalized();
				CurrentW.CastPosition = Prediction.CastPosition;

				PreCastE();
			}
		}

		void Clear()
		{
			if ( !W.IsReady() || GlobalClocks::LastUpdateNetworkTime() - LastJungleMines > 2.f )
				return;

			const float CurrentMana = ObjectManager::GetPlayer()->mResource()->mPAR.Value;

			if ( CurrentMana <= CostW[ W.Level() ] + CostE[ E.Level() ] )
				return;

			AIBaseClient* BestTarget = nullptr;
			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
			Prediction::PredictionResult BestResult;

			for ( const auto& Client : ManagerTemplates::Minions() )
			{
				if ( !Client || Client->IsDead() || !Client->IsValidTarget( W.Range, true, PlayerPosition ) || Client->IsLaneMinion() )
					continue;

				if ( !CanUseOnCamp( W, Client ) || Client->GetHealth() < 200.f )
					continue;

				Prediction::PredictionResult PredW;
				if ( !PredictionSuccess( GetPrediction(Client, W, PlayerPosition, PlayerPosition, true, &PredW) ) )
					continue;

				if ( !BestTarget )
				{
					BestTarget = Client;
					BestResult = PredW;
					continue;
				}

				if ( BestTarget->GetMaxHealth() < Client->GetMaxHealth() )
				{
					BestTarget = Client;
					BestResult = PredW;
				}
			}

			if ( BestTarget )
			{
				Vector3f Direction = BestResult.CastPosition.Extend( PlayerPosition, -10.f );

				// Gromp and Krug might go over the wall if we push them away.
				//
				switch ( BestTarget->SkinHash() )
				{
				case SkinHash::SRU_Gromp:
				case SkinHash::SRU_Krug:
				case SkinHash::Sru_Crab:
					Direction = BestResult.CastPosition.Extend( PlayerPosition, 10.f );
					break;
				default:
					break;
				}

				( void )W.CastSpell( BestResult.CastPosition, Direction );
				PreCastE();
			}
		}
	}

	namespace sE
	{
		float GetDamage( AIBaseClient* Client, const Vector3f Override = Vector3f::Zero, const Direction& Direction = TOWARDS )
		{
			const uint32_t LevelE = ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->Level();
			const float AP = ObjectManager::GetPlayer()->TotalAbilityPower();
			float Flat = BaseE[ LevelE ] + AP * RatioE;

			const float Distance = Override.x > 1.f ? ObjectManager::GetPlayer()->Position().Distance2D( Override ) : ObjectManager::GetPlayer()->Distance( Client->PositionOnPath() );
			const int MineAmount = Distance <= 266.f ? 2 : Distance <= 532.f ? 3 : Distance <= E.Range ? 1 : 0;
			Flat += Reduction[ MineAmount ] * ( MineE[ LevelE ] + AP * MineRatio );

			return Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, Flat );
		}

		void Clear()
		{
			if ( !E.IsReady() )
				return;

			AIBaseClient* BestTarget = nullptr;
			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
			Prediction::PredictionResult BestResult;

			for ( const auto& Client : ManagerTemplates::Minions() )
			{
				if ( !Client || Client->IsDead() || !Client->IsValidTarget( E.Range, true, PlayerPosition ) || Client->IsLaneMinion() )
					continue;

				if ( !CanUseOnCamp( E, Client ) || Client->GetHealth() < 200.f )
					continue;

				Prediction::PredictionResult PredE;
				if ( !PredictionSuccess( GetPrediction(Client, E, PlayerPosition, PlayerPosition, true, &PredE) ) )
					continue;

				if ( !BestTarget )
				{
					BestTarget = Client;
					BestResult = PredE;
					continue;
				}

				if ( BestTarget->GetMaxHealth() < Client->GetMaxHealth() )
				{
					BestTarget = Client;
					BestResult = PredE;
				}
			}

			if ( BestTarget && E.CastSpell( BestResult.CastPosition ) )
			{
				LastJungleMines = GlobalClocks::LastUpdateNetworkTime();
			}
		}
	}

	void PreCastE()
	{
		const float TimeForEToReach = E.CastDelay + CurrentW.CastPosition.Distance2D( ObjectManager::GetPlayer()->Position() ) / E.Range;
		const float Delay = std::clamp( 0.692f - TimeForEToReach, 0.25f, 0.6f ) + MultiplayerClient::GetInstance()->CurrentRoundTripTime();

		TaliyahActions.Add( [Delay]
		{
			if ( W.IsReady() ) // Assume that W didn't go through
				return;

			if ( ( Orbwalker::ActiveMode == Orbwalker::Mode::Combo && Menu::ComboE->Value ) || Menu::AutoE->Value )
			{
				const float RemainingTime = 0.692f - Delay;
				Prediction::PredictionData PredE;
				GetPositionAfterTime( CurrentW.Unit, RemainingTime, &PredE );

				if ( HasSpellShield( CurrentW.Unit, RemainingTime ) )
					return;

				const Vector3f TemporaryDirection = PredE.Position + CurrentW.Direction;

				if ( PredE.Position.Extend( TemporaryDirection, 350.f ).Distance2D( ObjectManager::GetPlayer()->Position() ) > E.Range )
					return;

				if ( PredE.Position.Distance2D( CurrentW.CastPosition ) <= W.Radius + 15.f )
					( void )E.CastSpell( ObjectManager::GetPlayer()->Position().Extend( CurrentW.CastPosition, 200.f ) );
			}
		}, Delay );
	}

	void AntiGapClose()
	{
		const Vector3f PlayerServerPosition = ObjectManager::GetPlayer()->PositionOnPath();

		for ( const auto& Target : ManagerTemplates::Heroes() )
		{
			if ( !Target || Target->IsDead() || Target->IsAlly() || !Target->IsVisible() )
				continue;

			if ( W.IsReady() && Menu::ModeW->Values[ 0 ] )
			{
				Prediction::PredictionResult PredW;

				if ( PredictionSuccess( GetPrediction(Target, W, PlayerServerPosition, PlayerServerPosition, true, &PredW) ) && ( PredW.Hitchance == Prediction::DashingMidAir || PredW.Hitchance == Prediction::DashingEnd ) && !HasSpellShield( Target, PredW.TimeToTarget ) )
				{
					Vector3f Direction = PredW.CastPosition.Extend( PlayerServerPosition, 10.f );

					if ( PredW.CastPosition.Distance2D( PlayerServerPosition ) <= W.Range / 2.f )
						Direction = PredW.CastPosition.Extend( PlayerServerPosition, -10.f );

					if ( sE::GetDamage( Target, PredW.CastPosition ) > Target->GetRealHealth( false, true ) )
						Direction = PredW.CastPosition.Extend( PlayerServerPosition, 10.f );

					( void )W.CastSpell( PredW.CastPosition, Direction );
					CurrentW.Unit = Target;
					CurrentW.LastChange = GlobalClocks::LastUpdateNetworkTime();
					CurrentW.Direction = ( Direction - PredW.CastPosition ).Normalized();

					return;
				}
			}

			if ( E.IsReady() && Menu::GapE->Value )
			{
				Prediction::PredictionResult PredE;

				if ( PredictionSuccess( GetPrediction(Target, E, PlayerServerPosition, PlayerServerPosition, true, &PredE) ) && ( PredE.Hitchance == Prediction::DashingMidAir || PredE.Hitchance == Prediction::DashingEnd ) )
				{
					( void )E.CastSpell( PredE.CastPosition );
				}
			}
		}
	}

	void Combo()
	{
		MagnetQ();
		sW::Combo();
		sQ::Combo();
	}

	void Clear()
	{
		sE::Clear();
		sW::Clear();
		sQ::Clear();
	}


	void OnGameUpdate()
	{
		UpdateSpells();
		AntiGapClose();
		TaliyahActions.Update();

		if ( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->Hash() == HASH( "TaliyahRSurfOptIn" ) && GlobalClocks::LastUpdateNetworkTime() - LastRide >= 1.f && Menu::AutoRide->Value )
		{
			( void )R.CastSpell();
			LastRide = GlobalClocks::LastUpdateNetworkTime();
		}

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

	void OnCreateObject( GameObject* Object )
	{
		const auto Emitter = Object->IsEffectEmitter() ? Object->AsEffectEmitter() : nullptr;

		if ( !Emitter )
			return;

		const std::string Name = Emitter->Name();

		if ( Name.contains( xorstr_( "Taliyah_" ) ) && Name.contains( xorstr_( "_Q_BuffGround" ) ) )
		{
			IsBoulder = true;
			Q = &Boulder;
		}
	}

	void OnDeleteObject( GameObject* Object )
	{
		const auto Emitter = Object->AsEffectEmitter();

		if ( !Emitter )
			return;

		const std::string Name = Emitter->Name();

		if ( Name.contains( xorstr_( "Taliyah_" ) ) && Name.contains( xorstr_( "_Q_BuffGround" ) ) )
		{
			IsBoulder = false;
			Q = &Volley;
		}
	}

	void OnProcessSpellCast( GameObject* Sender, Spell::SpellCastInfo* Spell )
	{
		if ( !Sender || !Sender->IsMe() )
			return;

		if ( Spell->Slot() == Spell::SpellSlot_Q && !IsBoulder )
		{
			MagnetUntil = GlobalClocks::LastUpdateNetworkTime() + 1.75f;
			VolleyDirection = ( Spell->EndPosition() - Spell->StartPosition() ).Normalized();
			DynamicDistance = Q->Range * .75f;
		}
	}

	void OnDraw()
	{
		const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

		if ( Menu::DrawR->Value && R.IsReady() && ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->Hash() == HASH( "TaliyahR" ) )
		{
			Graphics::AddCircleMinimap( PlayerPosition, R.Range, 1, Graphics::RGBA( 240, 128, 0, 255 ) );
			Graphics::AddCircle( PlayerPosition, R.Range, 1, Graphics::RGBA( 240, 128, 0, 255 ) );
		}

		if ( Menu::DrawQ->Value && ( Q->IsReady() || GlobalClocks::LastUpdateNetworkTime() <= MagnetUntil ) )
			Graphics::AddCircle( PlayerPosition, Q->Range, 1, Menu::ColorQ->Value );

		if ( Menu::DrawW->Value && W.IsReady() )
			Graphics::AddCircle( PlayerPosition, W.Range, 1, Menu::ColorQ->Value );

		if ( Menu::DrawE->Value && E.IsReady() )
			Graphics::AddCircle( PlayerPosition, E.Range, 1, Menu::ColorQ->Value );


		Vector2f PlayerScreenPosition;
		if ( !r3dProjectToScreen( PlayerPosition, PlayerScreenPosition ) )
			return;

		const char* DirectionW = CurrentDirection == AWAY ? xorstr_( "Semi [W] Direction: Push" ) : xorstr_( "Semi [W] Direction: Pull" );
		const Vector2f DirectionWSize = Graphics::TextSize( DirectionW, 16, true );

		PlayerScreenPosition.y += 30 - DirectionWSize.y / 2;
		PlayerScreenPosition.x -= DirectionWSize.x / 2;
		Graphics::AddText( DirectionW, 16, PlayerScreenPosition, Graphics::RGBA( 211, 211, 211, 211 ), true );
	}


	void Initialize()
	{
		Volley = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = 0.25f,
			.Range = 1000.0f,
			.RangeType = Center,
			.StartSpeed = 3600.0f,
			.EndSpeed = 1600.0f,
			.Acceleration = -5000.0f,
			.HasAcceleration = true,
			.Radius = 175.f / 2.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.TowerCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl,
			.ExtraCollisionRadius = 0.0f
		} );

		Boulder = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = 0.25f,
			.Range = 1000.0f,
			.RangeType = Center,
			.Speed = 2000.0f,
			.Radius = 225.f / 2.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.TowerCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl,
			.ExtraCollisionRadius = 30.0f
		} );

		Q = &Volley;

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_W,
			.Type = Circular,
			.CastDelay = .792f,
			.Range = 900.f,
			.RangeType = Center,
			.Speed = 20000.f,
			.Radius = 225.f
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_E,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 950.f,
			.RangeType = Center,
			.Speed = 1000.f,
			.Radius = 250.f
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_R,
			.Range = 100.f
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueTaliyah" ), xorstr_( "Blue Taliyah" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AP );

		const auto MenuQ = Main->AddMenu( xorstr_( "MenuQ" ), xorstr_( "[Q / Threaded Volley] Settings" ) );
		MenuQ->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );
		Menu::ComboQ = MenuQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
		Menu::MagnetQ = MenuQ->AddList( xorstr_( "MagnetQ" ), xorstr_( "Magnet Strength" ), { xorstr_( "Hard" ), xorstr_( "Soft" ), xorstr_( "None" ) }, 1 );
		Menu::AOEQ = MenuQ->AddCheckBox( xorstr_( "AOEQ" ), xorstr_( "Attempt AOE if Possible" ), false );

		MenuQ->AddInfo( xorstr_( "ClearSpacer" ), xorstr_( "Clear Settings" ) );
		Menu::JungleQ = MenuQ->AddMultiList( xorstr_( "JungleQ" ), xorstr_( "Use on Jungle Camps" ), { xorstr_( "Epic" ), xorstr_( "Large" ), xorstr_( "Small" ) }, { 0, 1 } );

		MenuQ->AddInfo( xorstr_( "DrawSpacer" ), xorstr_( "" ) );
		Menu::DrawQ = MenuQ->AddCheckBox( xorstr_( "DrawQ" ), xorstr_( "Draw Q Range" ), true );
		Menu::ColorQ = MenuQ->AddColorPicker( xorstr_( "ColorQ" ), xorstr_( "Draw Color" ), Graphics::RGBA( 139, 64, 0, 122 ) );

		const auto MenuW = Main->AddMenu( xorstr_( "MenuW" ), xorstr_( "[W / Seismic Shove] Settings" ) );
		MenuW->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_W )->GetIcon() );
		Menu::ComboW = MenuW->AddCheckBox( xorstr_( "ComboW" ), xorstr_( "Use in Combo" ), true );
		Menu::ModeW = MenuW->AddMultiList( xorstr_( "ModeW" ), xorstr_( "Usages" ), { xorstr_( "Anti Gap Close" ), xorstr_( "Cancel" ) }, { 0, 1, 2 } );
		Menu::SemiW = MenuW->AddKeyBind( xorstr_( "SemiW" ), xorstr_( "Semi W" ), 'T', false, false );
		Menu::ToggleW = MenuW->AddKeyBind( xorstr_( "ToggleW" ), xorstr_( "Toggle W Direction" ), VK_F6, true, true, UpdateMenu );

		MenuW->AddInfo( xorstr_( "ClearSpacer" ), xorstr_( "Clear Settings" ) );
		Menu::JungleW = MenuW->AddMultiList( xorstr_( "JungleW" ), xorstr_( "Use on Jungle Camps" ), { xorstr_( "Epic" ), xorstr_( "Large" ), xorstr_( "Small" ) }, { 1 } );

		MenuW->AddInfo( xorstr_( "DrawSpacer" ), xorstr_( "" ) );
		Menu::DrawW = MenuW->AddCheckBox( xorstr_( "DrawW" ), xorstr_( "Draw W Range" ), true );
		Menu::ColorW = MenuW->AddColorPicker( xorstr_( "ColorW" ), xorstr_( "Draw Color" ), Graphics::RGBA( 218, 165, 32, 122 ) );

		const auto MenuE = Main->AddMenu( xorstr_( "MenuE" ), xorstr_( "[E / Unraveled Earth] Settings" ) );
		MenuE->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );
		Menu::ComboE = MenuE->AddCheckBox( xorstr_( "ComboE" ), xorstr_( "Use in Combo" ), true );
		Menu::AutoE = MenuE->AddCheckBox( xorstr_( "AutoE" ), xorstr_( "Auto E on W casts if hits" ), false );
		Menu::GapE = MenuE->AddCheckBox( xorstr_( "GapE" ), xorstr_( "Anti Gap Close E" ), true );

		MenuE->AddInfo( xorstr_( "ClearSpacer" ), xorstr_( "Clear Settings" ) );
		Menu::JungleE = MenuE->AddMultiList( xorstr_( "JungleE" ), xorstr_( "Use on Jungle Camps" ), { xorstr_( "Epic" ), xorstr_( "Large" ), xorstr_( "Small" ) }, { 0, 1 } );

		MenuE->AddInfo( xorstr_( "DrawSpacer" ), xorstr_( "" ) );
		Menu::DrawE = MenuE->AddCheckBox( xorstr_( "DrawE" ), xorstr_( "Draw E Range" ), true );
		Menu::ColorE = MenuE->AddColorPicker( xorstr_( "ColorE" ), xorstr_( "Draw Color" ), Graphics::RGBA( 192, 64, 0, 122 ) );

		const auto MenuR = Main->AddMenu( xorstr_( "MenuR" ), xorstr_( "[R / Weaver's Wall] Settings" ) );
		MenuR->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->GetIcon() );

		Menu::AutoRide = MenuR->AddCheckBox( xorstr_( "AutoRide" ), xorstr_( "Auto Ride Wall" ), true );
		Menu::DrawR = MenuR->AddCheckBox( xorstr_( "DrawR" ), xorstr_( "Draw R Range" ), true );

		EventHandler::Add( Event::OnCreateObject, OnCreateObject );
		EventHandler::Add( Event::OnDeleteObject, OnDeleteObject );
		EventHandler::Add( Event::Present, OnDraw );
		EventHandler::Add( Event::OnProcessSpell, OnProcessSpellCast );
		EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );

		MenuSDK::PermaShow::AddComponent( xorstr_( "Semi W" ), Menu::SemiW );
		MenuSDK::PermaShow::AddComponent( xorstr_( "Change Semi W Direction" ), Menu::ToggleW );
	}
}
