#include "Annie.h"
#include "OrbwalkerSDK.h"

namespace Features::Champions::Annie
{
	float LastCastTime[ 4 ] = { 0.f, 0.f, 0.f, 0.f };
	bool IsR1();
	float TimeSinceLastCast( const BaseSpell& Spell );

#pragma region Damage

	BaseSpell Q, W, E, R;

	TargetSelector::Instance* TS;

	AIBaseClient* Tibbers = nullptr;
	bool IsTibbersAttacking = false;

	float LastAttack = 0.f;
	float NextMoveTime = 0.f;
	float NextAttackTime = 0.f;

	int QBase[ 6 ] = { 0, 80, 115, 150, 185, 220 };
	int QCost[ 6 ] = { 0, 60, 65, 70, 75, 80 };
	float QRatio = .8f;

	int WBase[ 6 ] = { 0, 70, 115, 160, 205, 250 };
	int WCost[ 6 ] = { 0, 90, 95, 100, 105, 110 };
	float WRatio = .85f;

	int ECost[ 6 ] = { 0, 60, 65, 70, 75, 80 };
	int EShield[ 6 ] = { 0, 60, 95, 130, 165, 200 };
	float EShieldRatio = .4f;
	int EBase[ 6 ] = { 0, 25, 35, 45, 55, 65 };
	float ERatio = .4f;

	int RCost = 100;
	int RBase[ 4 ] = { 0, 150, 275, 400 };
	float RRatio = .75f;

	float GetQDamage( AIBaseClient* Client )
	{
		return Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, QBase[ Q.Level() ] + QRatio * ObjectManager::GetPlayer()->TotalAbilityPower() );
	}

	float GetWDamage( AIBaseClient* Client )
	{
		return Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, WBase[ W.Level() ] + WRatio * ObjectManager::GetPlayer()->TotalAbilityPower() );
	}

	float GetRDamage( AIBaseClient* Client )
	{
		return Damage::CalculateMagicalDamage( ObjectManager::GetPlayer(), Client, RBase[ R.Level() ] + RRatio * ObjectManager::GetPlayer()->TotalAbilityPower() );
	}

	bool IsKillable( AIBaseClient* Client )
	{
		float TotalDamage = 0.f;

		if ( Q.IsReady() || TimeSinceLastCast( Q ) <= .5f )
			TotalDamage += GetQDamage( Client );

		if ( W.IsReady() || TimeSinceLastCast( W ) <= .25f )
			TotalDamage += GetWDamage( Client );

		if ( R.IsReady() || TimeSinceLastCast( R ) <= .25f )
			TotalDamage += GetRDamage( Client );

		return TotalDamage >= Client->GetRealHealth( false, true );
	}

#pragma endregion

	float TimeSinceLastCast( const BaseSpell& Spell )
	{
		switch ( Spell.Slot )
		{
		case Spell::kSpellSlot::SpellSlot_Q:
			return GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 0 ];
		case Spell::kSpellSlot::SpellSlot_W:
			return GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 1 ];
		case Spell::kSpellSlot::SpellSlot_E:
			return GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 2 ];
		case Spell::kSpellSlot::SpellSlot_R:
			return GlobalClocks::LastUpdateNetworkTime() - LastCastTime[ 3 ];
		default:
			return FLT_MAX;
		}
	}

	bool IsR1()
	{
		return strcmp( R.Name().c_str(), "AnnieR" ) == 0;
	}

	bool StunReady()
	{
		return ObjectManager::GetPlayer()->BuffManager()->HasBuff( HASH( "anniepassiveprimed" ) );
	}

	int GetPassiveStacks()
	{
		return ObjectManager::GetPlayer()->BuffManager()->HasBuff( HASH( "anniepassivestack" ) ) ? max( ObjectManager::GetPlayer()->BuffManager()->FindBuff(HASH("anniepassivestack"))->GetStacks(), 1 ) : 0;
	}

#pragma region Orbwalker

	// Laneclear
	std::unordered_map<uint32_t, AIBaseClient*> ToBeDead;

	void ClearMap()
	{
		for ( auto Entry = ToBeDead.begin(); Entry != ToBeDead.end(); )
		{
			if ( !Entry->second || Entry->second->IsDead() )
			{
				Entry = ToBeDead.erase( Entry );
				continue;
			}
			++Entry;
		}
	}

	void LaneclearQ( AIBaseClient* Client )
	{
		if ( Orbwalker::ActiveMode != Orbwalker::Mode::Laneclear || !Q.IsReady() || ToBeDead.contains( Client->NetworkID() ) )
			return;

		if ( Client->Distance( ObjectManager::GetPlayer() ) <= Q.Range && GetQDamage( Client ) >= Client->GetRealHealth( false, true ) )
		{
			Q.CastSpell( Client );
		}
	}

	// Combo
	void TibbersOrbwalker()
	{
		if ( !Tibbers || Tibbers->IsDead() || IsTibbersAttacking || IsR1() )
			return;

		const std::vector<AIBaseClient*> OrderedTargets = TS->GetOrderedTargets();

		if ( OrderedTargets.empty() )
			return;

		// Move
		if ( GlobalClocks::LastUpdateNetworkTime() >= NextMoveTime )
		{
			for ( const auto& Target : OrderedTargets )
			{
				if ( Target && Target->IsTargetable() && !Target->IsDead() && Target->IsEnemy() )
				{
					if ( Tibbers->Distance( Target ) <= Tibbers->GetAutoAttackRange() + ( GlobalClocks::LastUpdateNetworkTime() - NextAttackTime ) * Tibbers->CharacterIntermediate()->mMoveSpeed.Value )
					{
						if ( Target->GetPathController()->IsMoving() )
						{
							Vector3f Position = Target->Position().Extend( Target->GetPathController()->GetNavigationPath()->EndPosition, Tibbers->GetAutoAttackRange() );
							if ( R.CastSpell( Position ) )
								return;
						}
						Vector3f Position = Target->Position().Extend( ObjectManager::GetPlayer()->Position(), -85.f );
						if ( R.CastSpell( Position ) )
							return;
					}
				}
			}
		}

		// Attack
		if ( GlobalClocks::LastUpdateNetworkTime() >= NextAttackTime )
		{
			for ( const auto& Target : OrderedTargets )
			{
				if ( Target && Target->IsTargetable() && !Target->IsDead() && Target->IsEnemy() )
				{
					if ( Tibbers->IsInAutoAttackRange( Target ) )
					{
						R.CastSpell( Target );
						return;
					}
				}
			}
		}
	}

	bool IsOverkill( AIBaseClient* Client )
	{
		float TotalDamage = 0.f;

		if ( Q.IsReady() || TimeSinceLastCast( Q ) <= .5f )
			TotalDamage += GetQDamage( Client );

		if ( W.IsReady() || TimeSinceLastCast( W ) <= .25f )
			TotalDamage += GetWDamage( Client );

		return TotalDamage >= Client->GetRealHealth( false, true );
	}

	void HandleCombo( AIBaseClient* Client )
	{
		if ( !Client || !Client->IsTargetable() || Client->IsDead() || !Client->IsVisible() || Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
			return;

		// if (GetPassiveStacks() == 3)

		if ( !IsOverkill( Client ) && R.IsReady() && IsR1() )
		{
		}
	}

#pragma endregion

#pragma region Events

	void OnIterateMinions( AIBaseClient* Client )
	{
		if ( Client->SkinHash() == SkinHash::AnnieTibbers && Client->GetGoldRedirectTarget() == ObjectManager::GetPlayer() )
			Tibbers = Client;
	}

	void OnIterateHeroDraw( AIHeroClient* Client )
	{
	}

	void OnIterateMinionDraw( AIBaseClient* Client )
	{
	}

	void OnDraw()
	{
		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client && Client->IsOnScreen() && Client->IsVisible() )
			{
				// Draw DMG
			}
		}
	}

	void OnDrawWorld()
	{
		for ( const auto& Client : ManagerTemplates::Minions() )
		{
			if ( Client && Client->IsOnScreen() && Client->IsVisible() )
			{
				OnIterateMinionDraw( Client );
			}
		}

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client && Client->IsOnScreen() && Client->IsVisible() )
			{
				OnIterateHeroDraw( Client );
			}
		}

		if ( Tibbers && !Tibbers->IsDead() )
		{
			Graphics::AddCircleFilled( Tibbers->Position(), Tibbers->GetBoundingRadius() * 1.3f, Graphics::RGBA( 222, 49, 99, 45 ) );
			Graphics::AddCircle( Tibbers->Position(), Tibbers->GetBoundingRadius() * 1.3f, 2, Graphics::RGBA( 222, 49, 99, 255 ) );
		}
	}

	void OnAfterAttack( GameObject* Target )
	{
		if ( Target && Target->IsMinion() )
		{
			const float AADamage = Damage::CalculateAutoAttackDamage( ObjectManager::GetPlayer(), Target->AsAttackableUnit() );
			if ( Target->AsAttackableUnit()->mHealth()->mHP.Value <= AADamage )
				ToBeDead.emplace( Target->NetworkID(), Target->AsAIBase() );
		}
	}

	void OnBasicAttack( AIBaseClient* Sender, Spell::SpellCastInfo* Spell )
	{
		if ( Sender && Sender == Tibbers )
		{
			LastAttack = GlobalClocks::LastUpdateNetworkTime();
			NextMoveTime = GlobalClocks::LastUpdateNetworkTime() + Sender->ComputeCharacterAttackCastDelay() + 0.05f;
			NextAttackTime = GlobalClocks::LastUpdateNetworkTime() + Sender->ComputeCharacterAttackDelay();
			IsTibbersAttacking = true;
		}
	}

	void OnGameUpdate()
	{
		if ( !Tibbers || Tibbers->IsDead() )
			Tibbers = nullptr;

		TibbersOrbwalker();

		if ( GlobalClocks::LastUpdateNetworkTime() >= NextMoveTime && IsTibbersAttacking )
		{
			IsTibbersAttacking = false;
		}

		for ( const auto& Base : ManagerTemplates::AIBases() )
		{
			if ( Base->IsMinion() )
			{
				OnIterateMinions( Base->AsAIBase() );
			}
		}
	}

#pragma endregion

	void Initialize()
	{
		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueAnnie" ), xorstr_( "Blue Annie" ) );

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AP );

		EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );
		EventHandler::Add( Event::OnBasicAttack, OnBasicAttack );
		EventHandler::Add( Event::WorldDraw, OnDrawWorld );
		EventHandler::Add( Event::Present, OnDraw );
		// EventHandler::Add(Event::OnOrbwalkerAction, OnAfterAttack);
		// EventHandler::Add(Event::OnGameUpdate, OnGameUpdate);

		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.CastDelay = .25f,
			.Range = 625.f,
			.RangeType = Center,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Conical,
			.CastDelay = .25f,
			.Range = 600.f,
			.RangeType = Edge,
			.Speed = FLT_MAX,
			.Radius = 49.52f
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.CastDelay = .0f,
			.Range = 800.f,
			.RangeType = Center,
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.Type = Circular,
			.CastDelay = .25f,
			.Range = 600.f,
			.RangeType = Center,
			.Speed = FLT_MAX,
			.Radius = 250.f
		} );
	}
}
