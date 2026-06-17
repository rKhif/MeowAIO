#include "Lulu.h"

#include <ranges>

#include "GameClient.h"
#include "BuffCache.h"

namespace Features::Champions::Lulu
{
	std::vector<TrackedBuff> TrackedBuffHashList;

	void FindPix()
	{
		if ( Pix )
			return;

		for ( const auto& Minion : ManagerTemplates::Minions() )
		{
			if ( strcmp( Minion->Name().c_str(), xorstr_( "RobotBuddy" ) ) == 0 && Minion->GetGoldRedirectTarget() == ObjectManager::GetPlayer() )
			{
				Pix = Minion;
				break;
			}
		}
	}

	void OnChangeMenu( ... )
	{
		// W Boost
		Menu::BoostSpacer->Hide( !Menu::ComboW->Values[ 0 ] );
		Menu::SemiBoost->Hide( !Menu::ComboW->Values[ 0 ] );

		// W Poly
		Menu::PolySpacer->Hide( !Menu::ComboW->Values[ 1 ] );
		Menu::PolySuperiority->Hide( !Menu::ComboW->Values[ 1 ] );
		Menu::PolyMarksman->Hide( !Menu::ComboW->Values[ 1 ] );
		Menu::PolyBruiser->Hide( !Menu::ComboW->Values[ 1 ] );
		Menu::PolyBurst->Hide( !Menu::ComboW->Values[ 1 ] );
		Menu::PolyMelee->Hide( !Menu::ComboW->Values[ 1 ] );

		// E Shield
		Menu::ShieldSpacer->Hide( !Menu::ComboE->Values[ 0 ] );
		Menu::BlockDamage->Hide( !Menu::ComboE->Values[ 0 ] );
		Menu::HeliaLogic->Hide( !Menu::ComboE->Values[ 0 ] );
		Menu::UseShieldMenu->Hide( !Menu::ComboE->Values[ 0 ] );

		// E Damage
		Menu::DamageSpacer->Hide( !Menu::ComboE->Values[ 1 ] );

		// R
		Menu::UseRMenu->Hide( !Menu::ComboR->Value );
		Menu::OnKillableR->Hide( !Menu::ComboR->Value );
		Menu::AntiGapCloseR->Hide( !Menu::ComboR->Value );
		Menu::AOER->Hide( !Menu::ComboR->Value );
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

	namespace Spell_Q
	{
		const std::function TargetFilter = [&] ( AIBaseClient* Target )
		{
			return !Target->mCharacterState()->IsUnstoppable();
		};

		const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* Prediction )
		{
			if ( !PredictionSuccess( Status ) )
				return false;

			return Prediction->Hitchance >= Prediction::High;
		};

		bool Combo()
		{
			if ( !Q.IsReady() || ObjectManager::GetPlayer()->IsWindingUp() || !Menu::ComboQ->Value || ObjectManager::GetPlayer()->IsDead() )
				return false;

			Prediction::PredictionResult BestPred{ .Hitchance = Prediction::Impossible };

			// From Lulu
			if ( Menu::ComboQLocation->Values[ 0 ] )
			{
				auto BestTarget = TS->GetBestTarget( Q, ObjectManager::GetPlayer()->Position(), TargetFilter, PredFilter );
				if ( BestTarget.first )
				{
					Prediction::PredictionResult LuluPred;
					if ( PredictionSuccess( Prediction::GetPrediction( BestTarget.first, Q, ObjectManager::GetPlayer()->Position(), ObjectManager::GetPlayer()->Position(), true, &LuluPred) ) && LuluPred.Hitchance > BestPred.Hitchance )
					{
						BestPred = LuluPred;
					}
				}
			}

			// From Pix
			if ( Menu::ComboQLocation->Values[ 1 ] && Pix )
			{
				auto BestTarget = TS->GetBestTarget( Q, Pix->Position(), TargetFilter, PredFilter );
				if ( BestTarget.first )
				{
					Prediction::PredictionResult LuluPred;
					if ( PredictionSuccess( Prediction::GetPrediction(BestTarget.first, Q, Pix->Position(), Pix->Position(), true, &LuluPred) ) && LuluPred.Hitchance > BestPred.Hitchance )
					{
						BestPred = LuluPred;
					}
				}
			}

			if ( BestPred.Hitchance >= GetHitchance( Menu::HitchanceQ ) && Q.CastSpell( BestPred.CastPosition ) )
				return true;

			return false;
		}
	}

	namespace Spell_W
	{
		struct AATimer
		{
			float Time;
			uint32_t TargetNetworkID;
		};

		float GetAngle( Vector3f Direction1, Vector3f Direction2 )
		{
			const auto DeltaPosDeltaPos = Direction1 - Direction2;
			const auto Angle = MathUtil::RadiansToDegrees( std::atan2f( DeltaPosDeltaPos.x, DeltaPosDeltaPos.z ) );

			if ( Angle < 0 )
				return Angle + 360.f;

			return Angle;
		}

		bool IsPointInCone( Vector3f StartPosition, Vector3f EndPosition, Vector3f Position, float Angle )
		{
			const auto Delta = std::fabsf( GetAngle( StartPosition, Position ) - GetAngle( StartPosition, EndPosition ) );

			return Delta < Angle;
		}

		bool IsFacing( AIBaseClient* Unit, Vector3f Position, Vector3f UnitDirection = Riot::Vector3f::Zero, float Angle = 60.f )
		{
			const auto UnitPosition = Unit->Position();
			const auto Direction = UnitDirection.IsNonZeroValid() ? UnitDirection : Unit->Direction();
			const auto EndPosition = UnitPosition + Direction * 10.f;

			return IsPointInCone( UnitPosition, EndPosition, Position, Angle / 2 );
		}

		std::unordered_map<uint32_t, std::pair<AATimer, AATimer>> LastAAOnHero;

		bool CanPolymorph( AIBaseClient* Client )
		{
			// Initial Checks
			//
			if ( !W.IsReady() || !Menu::ComboW->Values[ 1 ] )
				return false;

			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

			// Target Validity Checks
			//
			if ( !Client->IsValidTarget( W.Range, true, PlayerPosition ) || Client->mCharacterState()->IsUnstoppable() )
				return false;

			// Numerical Superiority
			//
			constexpr float RangeToCheck = 1250.f * 1250.f;
			if ( AllyHeroTargetsInRange( PlayerPosition, RangeToCheck ) > EnemyHeroTargetsInRange( PlayerPosition, RangeToCheck ) && Menu::PolySuperiority->Value )
				return true;

			// Anti-Melee
			//
			if ( Client->IsMelee() && EnemyHeroTargetsInRange( Client, pow( Client->GetAutoAttackRange() + 100.f, 2 ) ) > 0 && Menu::PolyMelee->Value )
				return true;

			// Class-Based
			//
			if ( IsMarksman( Client ) || IsFighter(Client) )
			{
				if (LastAAOnHero.contains(Client->NetworkID()))
				{
					auto& [Timer1, Timer2] = LastAAOnHero[Client->NetworkID()];
					auto& [Time, TargetNetworkID] = Timer1.Time > Timer2.Time ? Timer1 : Timer2;

					// Hasn't AA'd a Champion in 1s
					if (GlobalClocks::LastUpdateNetworkTime() - Time > 1.f)
						return false;
				}

				const bool Value = IsMarksman(Client) ? Menu::PolyMarksman->Value : Menu::PolyBruiser->Value;

				return Value;
			}

			if ( IsBurst( Client ) )
				return Menu::PolyBurst->Value;

			return false;
		}

		const std::function TargetFilter = [&] ( AIBaseClient* Target )
		{
			return CanPolymorph( Target ) && !Target->mCharacterState()->IsUnstoppable();
		};

		const std::function PredFilter = [&] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* Prediction )
		{
			if ( !PredictionSuccess( Status ) )
				return false;

			return Prediction->Hitchance >= Prediction::Low && !HasSpellShield( Target, Prediction->TimeToTarget );
		};

		void HandleChaseW(AIBaseClient* Ally, GameObject* Target)
		{
			if (!W.IsReady() || !Menu::ComboW->Values[0] || IsSupport(Ally) || IsMage(Ally))
				return;

			if (!Target)
				return;

			AIBaseClient* Enemy = Target->AsAIBase();

			if (!Enemy || Enemy->IsDead() || !Enemy->IsTargetable() || !IsFacing(Ally, Enemy->Position()))
				return;

			const float Range = Ally->GetAutoAttackRange(Enemy);
			const float Delay = Ally->ComputeCharacterAttackDelay() + .06f;
			const float TimeTillNextAA = Ally->ComputeCharacterAttackDelay();

			Prediction::PredictionData Pred;
			GetPositionAfterTime(Enemy, Delay, &Pred);

			const float DistanceAfterAA = Pred.Position.Distance2D(Ally->Position());

			if (DistanceAfterAA < Range)
				return;

			const float AllyTravel = Ally->CharacterIntermediate()->MoveSpeed() * TimeTillNextAA;
			const float AllyTravel2 = 1.25f * Ally->CharacterIntermediate()->MoveSpeed() * TimeTillNextAA;
			const float EnemyTravel = Ally->CharacterIntermediate()->MoveSpeed() * TimeTillNextAA;

			const float Difference = DistanceAfterAA - Range;
			const bool CanReachWithoutBoost = AllyTravel >= EnemyTravel + Difference;
			const bool CanReachWithBoost = AllyTravel2 >= EnemyTravel + Difference;

			if (CanReachWithoutBoost)
				return;

			if (CanReachWithBoost)
				W.CastSpell(Ally);
		}

		void OnBasicAttack(AIBaseClient* Sender, Spell::SpellCastInfo* Spell)
		{
			if (!Sender->IsHero() || Sender->IsMe())
				return;

			GameObject* Target = Spell->GetTarget();

			if (!Target || !Target->IsHero())
				return;

			if (Sender->IsAlly() && Menu::ChaseLogic->Value && Orbwalker::ActiveMode == Orbwalker::Mode::Combo)
				HandleChaseW(Sender, Target);

			const uint32_t NetworkID = Sender->NetworkID();

			if (!LastAAOnHero.contains(NetworkID))
			{
				LastAAOnHero[NetworkID] = { { GlobalClocks::LastUpdateNetworkTime(), Target->NetworkID() }, { 0.f, 0 } };
				return;
			}

			auto& [Timer1, Timer2] = LastAAOnHero[NetworkID];
			auto& [Time, TargetNetworkID] = Timer1.Time <= Timer2.Time ? Timer1 : Timer2;

			Time = GlobalClocks::LastUpdateNetworkTime();
			TargetNetworkID = Target->NetworkID();
		}

		// Polymorph Scenarios
		//
		bool Combo()
		{
			if ( !W.IsReady() )
				return false;

			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

			const auto OrderedTargets = TS->GetBestTargets( W, PlayerPosition, TargetFilter, PredFilter );

			for ( const auto Client : OrderedTargets | std::views::keys )
			{
				if ( !Client->IsValidTarget( W.Range, true, ObjectManager::GetPlayer()->Position() ) )
					continue;

				if ( CanPolymorph( Client ) && W.CastSpell( Client ) )
					return true;
			}

			return false;
		}

		bool PassesChecks(AIBaseClient* Ally, GameObject* Target)
		{
			/* Basic Sanity Check */
			if (!Target)
				return false;

			AIBaseClient* Client = Target->AsAIBase();

			if (!Client || Client->IsDead() || !Client->IsTargetable())
				return false;

			if (!Ally || Ally->IsDead() || IsSupport(Ally))
				return false;

			/* Get our Ally Attack Cast + Delay, Query Pred */
			const float AutoAttackRange = Ally->GetAutoAttackRange(Client);
			const float Delay = Ally->ComputeCharacterAttackDelay();
			const float SimulationTime = Delay + Ally->ComputeCharacterAttackCastDelay();

			Prediction::PredictionData Pred;
			GetPositionAfterTime(Client, SimulationTime, &Pred);

			/* Get the Distance the Enemy can travel while Ally AAs (and until Ally's next AA is ready) */
			const float MoveSpeed = Ally->CharacterIntermediate()->MoveSpeed();
			const float EnemyTravelledDist = Client->CharacterIntermediate()->MoveSpeed() * SimulationTime;
			const float DistanceAfterPred = Ally->Position().Distance2D(Pred.Position);

			/* Calculate the Distance Ally will be able to Travel (and do the same for if we boost them) */
			const float DistanceTraveledWithoutBoost = MoveSpeed * SimulationTime;
			const float DistanceTraveledWithBoost = (MoveSpeed * 1.25f) * SimulationTime;

			/* Get the Realistic Distance when Ally gets an Auto-Attack Ready */
			const float DistanceToEnemyWithoutBoost = DistanceAfterPred + EnemyTravelledDist - DistanceTraveledWithoutBoost;
			const float DistanceToEnemyWithBoost = DistanceAfterPred + EnemyTravelledDist - DistanceTraveledWithBoost;

			/* If our Ally is currently in Range, but AA'ing will put them out of range for Next AA, but our boost compensates for that loss, return true */
			return DistanceAfterPred > AutoAttackRange && DistanceToEnemyWithoutBoost > AutoAttackRange && DistanceToEnemyWithBoost <= AutoAttackRange;
		}

		// Cancel Spells and GapClose + AutoBoost
		//
		bool OnIterateChampions( AIHeroClient* Client )
		{
			if ( !W.IsReady() || !Client->IsValidTarget( W.Range, false, ObjectManager::GetPlayer()->Position() ) )
				return false;

			const uint32_t NetworkID = Client->NetworkID();

			if ( Client->IsAlly() )
			{
				if ( !Menu::ComboW->Values[ 0 ] )
					return false;

				auto& [Timer1, Timer2] = LastAAOnHero[Client->NetworkID()];
				auto& [Time, TargetNetworkID] = Timer1.Time > Timer2.Time ? Timer1 : Timer2;

				switch ( Client->SkinHash() )
				{
				case SkinHash::Ashe:
				case SkinHash::Kaisa:
				case SkinHash::KogMaw:
				case SkinHash::Twitch:
				case SkinHash::Vayne:
				case SkinHash::Samira:
				case SkinHash::Xayah:
				case SkinHash::Jinx:
				case SkinHash::Zeri:
					if (GlobalClocks::LastUpdateNetworkTime() - Time <= 1.f)
					{
						for (const auto Buff : TrackedBuffHashList)
						{
							if (Buff.Champion != Client->SkinHash())
								continue;

							if (Client->BuffManager()->HasBuff(Buff.Hash) && Buff.CanW() && W.CastSpell(Client))
							{
								return true;
							}
						}
					}
					break;
				default:
					break;
				}

				if (!LastAAOnHero.contains(NetworkID) || Client->IsMe() || !Menu::ChaseLogic->Value || Orbwalker::ActiveMode != Orbwalker::Mode::Combo)
					return false;
				ObjectManager* const Instance = ObjectManager::GetInstance();

				if (!Instance)
					return false;

				GameObject* Target1 = Timer1.TargetNetworkID != 0 ? Instance->GetNetworkObject(Timer1.TargetNetworkID) : nullptr;
				GameObject* Target2 = Timer2.TargetNetworkID != 0 ? Instance->GetNetworkObject(Timer2.TargetNetworkID) : nullptr;

				if ((PassesChecks(Client, Target1) || PassesChecks(Client, Target2)) && W.CastSpell(Client))
					return true;
			}

			if (!Menu::ComboW->Values[1])
				return false;

			if (Menu::CancelW->Value && CanCancel(W, Client, Menu::CancelWValue->Value) && W.CastSpell(Client))
				return true;

			// Disable anti-gapclose for these two
			//
			if (Client->SkinHash() == SkinHash::Kalista || Client->SkinHash() == SkinHash::Yasuo)
				return false;

			// Champion is dashing and not unstoppable
			//
			if (!Client->GetPathController()->IsDashing() || Client->mCharacterState()->IsUnstoppable() || HasSpellShield(Client, (Client->Distance(ObjectManager::GetPlayer()) / W.Speed) * 1.25f))
				return false;

			const float DashDistance = Client->GetPathController()->GetNavigationPath()->Length();

			if (DashDistance >= 200.f && W.CastSpell(Client))
				return true;

			return false;
		}
	}

	namespace Spell_E
	{
		constexpr float ShieldBase[7] = { 0.f, 75.f, 115.f, 155.f, 195.f, 235.f };
		constexpr float ShieldRatio = .55f;

		constexpr float DamageBase[7] = { 0.f, 80.f, 120.f, 160.f, 200.f, 240.f, 280.f };
		constexpr float DamageRatio = .5f;

		bool CanShield( AIBaseClient* Client )
		{
			if ( !Client || !Client->IsValidTarget( E.Range, false, ObjectManager::GetPlayer()->Position() ) || !E.IsReady() || Client->IsEnemy() )
				return false;

			return Menu::UseShieldOn.contains( Client->NetworkID() ) && Menu::UseShieldOn[ Client->NetworkID() ]->Value;
		}

		float GetShield(AIBaseClient* Client)
		{
			float BaseValue = ShieldBase[E.Level()] + ShieldRatio * ObjectManager::GetPlayer()->TotalAbilityPower();

			const float ShieldPower = ObjectManager::GetPlayer()->GetHealShieldPowerPercentage();
			BaseValue *= 1.f + ShieldPower;

			// Shield Reduction
			if (Client)
			{
				BuffInstance* Instance = Client->FindBuff(HASH("serpentvenom"));
				if (Instance && Instance->IsActive())
				{
					AIBaseClient* Caster = Instance->GetCaster();
					BaseValue *= Caster->IsMelee() ? .5f : .75f;
				}
			}

			return BaseValue;
		}

		float GetDamage(AIBaseClient* Client)
		{
			return Damage::CalculateMagicalDamage(ObjectManager::GetPlayer(), Client, DamageBase[E.Level()] + DamageRatio * ObjectManager::GetPlayer()->TotalAbilityPower());
		}

		bool OnIterateChampions( AIHeroClient* Client )
		{
			if ( !E.IsReady() || !Client->IsValidTarget(E.Range, false, ObjectManager::GetPlayer()->Position()))
				return false;

			const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();

			// E Shield Scenarios
			//
			if ( Client->IsAlly() )
			{
				if ( !Menu::ComboE->Values[ 0 ] || !CanShield( Client ) )
					return false;

				// Buff Handler
				//
				{
					switch ( Client->SkinHash() )
					{
					case SkinHash::Ashe:
					case SkinHash::Kaisa:
					case SkinHash::KogMaw:
					case SkinHash::Twitch:
					case SkinHash::Vayne:
					case SkinHash::Samira:
					case SkinHash::Xayah:
					case SkinHash::Jinx:
					case SkinHash::Zeri:
						{
							auto& [Timer1, Timer2] = Spell_W::LastAAOnHero[Client->NetworkID()];
							auto& [Time, TargetNetworkID] = Timer1.Time > Timer2.Time ? Timer1 : Timer2;

							if (GlobalClocks::LastUpdateNetworkTime() - Time <= 1.f )
							{
								for (const auto Buff : TrackedBuffHashList)
								{
									if (Buff.Champion != Client->SkinHash())
										continue;

									if (Client->BuffManager()->HasBuff(Buff.Hash) && Buff.CanE() && E.CastSpell(Client))
									{
										return true;
									}
								}
							}
						}
					default:
						break;
					}
				}

				// Block Damage
				//
				const float IncomingDamage = Damage::GetIncomingDamage( Client, .75f );
				const bool ShouldShield = GetShield( Client ) * .5f <= IncomingDamage || IncomingDamage > Client->GetHealth();

				if ( Menu::BlockDamage->Value && ShouldShield && E.CastSpell( Client ) )
					return true;

				// Echoes of Helia Logic
				//
				const auto Instance = BuffCache::GetBuff( ObjectManager::GetPlayer(), HASH( "6620soulbuff" ) );
				if ( Instance && max( 1, Instance->GetStacks() ) >= Menu::HeliaLogic->Value && E.CastSpell( Client ) )
					return true;
			}

			// E Damage Scenarios
			//
			if ( Client->IsEnemy() && Orbwalker::ActiveMode == Orbwalker::Mode::Combo )
			{
				if ( !Menu::ComboE->Values[ 1 ] )
					return false;

				AIBaseClient* BestAlly = ObjectManager::GetPlayer();

				for (const auto& Ally : ManagerTemplates::Heroes())
				{
					if (!Client->IsAlly() || !Client->IsValidTarget(E.Range, false, PlayerPosition) || (!IsMarksman(Client) && !IsBurst(Client)))
						continue;

					BestAlly = Ally;
				}

				const float DamageValue = GetDamage(Client) / Client->GetHealth();
				const float ShieldValue = GetShield(BestAlly) / BestAlly->GetHealth();

				if (ShieldValue > DamageValue)
					return false;

				if ( E.CastSpell( Client ) )
					return true;
			}

			return false;
		}
	}

	namespace Spell_R
	{
		bool CanUlt( AIBaseClient* Client )
		{
			if ( !Client || !Client->IsValidTarget( R.Range, false, ObjectManager::GetPlayer()->Position() ) || !R.IsReady() || Client->IsEnemy() )
				return false;

			return Menu::UseROn.contains( Client->NetworkID() ) && Menu::UseROn[ Client->NetworkID() ]->Value;
		}

		bool OnIterateChampions( AIHeroClient* Client )
		{
			if ( !R.IsReady() || !Client->IsValidTarget( R.Range, false, ObjectManager::GetPlayer()->Position() ) || !CanUlt( Client ) )
				return false;

			if ( Client->IsAlly() )
			{
				if ( Menu::OnKillableR->Value )
				{
					// Low HP
					//
					// TODO: Discard when DMGLib done
					if ( Orbwalker::ActiveMode == Orbwalker::Mode::Combo && Client->GetHealthPercentage() <= 25 && EnemyHeroTargetsInRange( Client, pow( 800, 2 ) ) > 0 && R.CastSpell( Client ) )
						return true;

					// Incoming Damage
					//
					const float IncomingDamage = Damage::GetIncomingDamage( Client, 1.5f );
					const bool ShouldUlt = IncomingDamage >= Client->GetHealth() || Client->GetHealth() - IncomingDamage <= Client->GetMaxHealth() * .1f;
					if ( ShouldUlt && R.CastSpell( Client ) )
						return true;
				}

				// AOE R
				//
				if ( EnemyHeroTargetsInRange( Client, pow( 400, 2 ) ) >= Menu::AOER->Value && R.CastSpell( Client ) )
					return true;

				// Cancel R + AntiGapClose
				//
				for ( const auto& Hero : ManagerTemplates::Heroes() )
				{
					if ( !Hero->IsValidTarget( 400, true, Client->Position() ) )
						continue;

					// CC'able
					//
					if ( HasSpellShield( Hero, 0.1f ) || Hero->mCharacterState()->IsUnstoppable() )
						continue;

					// Cancel R
					//
					if ( Menu::CancelR->Value && CanCancel( R, Hero, Menu::CancelRValue->Value, false ) && R.CastSpell( Client ) )
						return true;

					// GapClose R
					//
					if ( Menu::AntiGapCloseR->Value )
					{
						if ( !Hero->GetPathController()->IsDashing() )
							continue;

						const float DashDistance = Hero->GetPathController()->GetNavigationPath()->Length();

						if ( DashDistance >= 200.f && R.CastSpell( Client ) )
							return true;
					}
				}

				// Fiddle R Logic
				//
				if ( IsMarksman( Client ) || IsBurst( Client ) ) // lol
				{
					for ( const auto& Fiddle : ManagerTemplates::Heroes() )
					{
						if ( Fiddle->IsAlly() || Fiddle->SkinHash() != SkinHash::Fiddlesticks )
							continue;

						if ( BuffCache::HasBuff( Fiddle, HASH( "FiddleSticksR" ) ) && Client->Position().Distance2D( Fiddle->Position() ) <= R.Radius && R.CastSpell( Client ) )
							return true;
					}
				}
			}

			return false;
		}
	}

	void OnIterateChampions( AIHeroClient* Client )
	{
		Spell_R::OnIterateChampions( Client );
		Spell_E::OnIterateChampions( Client );
		Spell_W::OnIterateChampions( Client );
	}

	void OnGameUpdate()
	{
		FindPix();

		if ( Orbwalker::ActiveMode != Orbwalker::Mode::Combo )
			return;

		Spell_W::Combo();
		Spell_Q::Combo();
	}

	void Initialize()
	{
		Q = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 950.f,
			.RangeType = Center,
			.Speed = 1450.f,
			.Radius = 60.f,
			.HeroCollisionType = SpellCollisionType::None,
			.MinionCollisionType = SpellCollisionType::None,
			.TowerCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		W = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_W,
			.Type = Targeted,
			.CastDelay = .2419f,
			.Range = 650.f,
			.RangeType = Center,
			.Speed = 2250.f,
			.Radius = 10.f,
			.HeroCollisionType = SpellCollisionType::None,
			.MinionCollisionType = SpellCollisionType::None,
			.TowerCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideWindWall | CollideShield | CollideBladeWhirl
		} );

		E = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_E,
			.Type = Targeted,
			.CastDelay = .0f,
			.Range = 650.f,
			.RangeType = Center,
			.Speed = FLT_MAX
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::kSpellSlot::SpellSlot_R,
			.Type = Targeted,
			.CastDelay = .0f,
			.Range = 950.f,
			.RangeType = Center,
			.Speed = FLT_MAX,
			.Radius = 400.f
		} );

		const auto Main = MenuSDK::Menu::CreateMenu( xorstr_( "BlueLulu" ), xorstr_( "Blue Lulu" ) );
		Main->AddTexture( ObjectManager::GetPlayer()->GetIconSquare() );

		const auto QMenu = Main->AddMenu( xorstr_( "QMenu" ), xorstr_( "[Q / Glitterlance]" ) );
		QMenu->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_Q )->GetIcon() );

		Menu::ComboQ = QMenu->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Allow [Q] Usage" ), true );
		Menu::ComboQLocation = QMenu->AddMultiList( xorstr_( "ComboQLocation" ), xorstr_( "Prediction Origins" ), { xorstr_( "Lulu" ), xorstr_( "Pix" ) }, { 0, 1 } );
		Menu::HitchanceQ = QMenu->AddList( xorstr_( "HitchanceQ" ), xorstr_( "Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 2 );
		Menu::DrawQ = QMenu->AddCheckBox( xorstr_( "DrawQ" ), xorstr_( "Draw Range" ) );

		const auto WMenu = Main->AddMenu( xorstr_( "WMenu" ), xorstr_( "[W / Whimsy]" ) );
		WMenu->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_W )->GetIcon() );

		Menu::ComboW = WMenu->AddMultiList( xorstr_( "ComboW" ), xorstr_( "[W] Usage" ), { xorstr_( "Speed" ), xorstr_( "Polymorph" ) }, { 1 }, OnChangeMenu );

		Menu::BoostSpacer = WMenu->AddInfo( xorstr_( "BoostSpacer" ), xorstr_( "[W] Boost Settings" ) );
		Menu::ChaseLogic = WMenu->AddCheckBox(xorstr_("ChaseLogic"), xorstr_("Use for Ally to Catch Up"), true);
		Menu::SemiBoost = WMenu->AddKeyBind( xorstr_( "SemiBoost" ), xorstr_( "Semi-[W] Boost Key" ), 'W', false, false );

		Menu::PolySpacer = WMenu->AddInfo( xorstr_( "PolySpacer" ), xorstr_( "[W] Polymorph Settings" ) );
		Menu::PolySuperiority = WMenu->AddCheckBox( xorstr_( "PolySuperiority" ), xorstr_( "Polymorph in Numerical Superiority" ), true );
		Menu::PolyMarksman = WMenu->AddCheckBox( xorstr_( "PolyMarksman" ), xorstr_( "Polymorph ADCs" ), true );
		Menu::PolyBruiser = WMenu->AddCheckBox( xorstr_( "PolyBruiser" ), xorstr_( "Polymorph Bruisers" ), false );
		Menu::PolyBurst = WMenu->AddCheckBox( xorstr_( "PolyBurst" ), xorstr_( "Polymorph Assassins/Burst Mages" ), true );
		Menu::PolyMelee = WMenu->AddCheckBox( xorstr_( "PolyMelee" ), xorstr_( "Polymorph Melees when in Ally Range" ), true );

		const auto EMenu = Main->AddMenu( xorstr_( "EMenu" ), xorstr_( "[E / Help, Pix!]" ) );
		EMenu->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_E )->GetIcon() );

		Menu::ComboE = EMenu->AddMultiList( xorstr_( "ComboE" ), xorstr_( "[E] Usage" ), { xorstr_( "Shield" ), xorstr_( "Damage" ) }, { 0 }, OnChangeMenu );

		Menu::ShieldSpacer = EMenu->AddInfo( xorstr_( "ShieldSpacer" ), xorstr_( "[E] Shield Settings" ) );
		Menu::BlockDamage = EMenu->AddCheckBox( xorstr_( "BlockDamage" ), xorstr_( "Block Damage with [E]" ), true );
		Menu::UseShieldMenu = EMenu->AddMenu( xorstr_( "UseShieldMenu" ), xorstr_( "Use [Shield] on:" ) );
		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client->IsAlly() )
			{
				Menu::UseShieldOn[ Client->NetworkID() ] = Menu::UseShieldMenu->AddCheckBox( Client->SkinName(), GetDisplayName( Client ), true );
				Menu::UseShieldOn[ Client->NetworkID() ]->AddTexture( Client->GetIconSquare() );
			}
		}

		Menu::DamageSpacer = EMenu->AddInfo( xorstr_( "DamageSpacer" ), xorstr_( "[E] Damage Settings" ) );

		const auto RMenu = Main->AddMenu( xorstr_( "RMenu" ), xorstr_( "[R / Wild Growth]" ) );
		RMenu->AddTexture( ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->GetIcon() );

		Menu::ComboR = RMenu->AddCheckBox( xorstr_( "ComboR" ), xorstr_( "Allow [R] Usage" ), true );

		Menu::UseRMenu = RMenu->AddMenu( xorstr_( "UseRMenu" ), xorstr_( "Use [R] on:" ) );
		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( Client->IsAlly() )
			{
				Menu::UseROn[ Client->NetworkID() ] = Menu::UseRMenu->AddCheckBox( Client->SkinName(), GetDisplayName( Client ), true );
				Menu::UseROn[ Client->NetworkID() ]->AddTexture( Client->GetIconSquare() );
			}
		}

		Menu::OnKillableR = RMenu->AddCheckBox( xorstr_( "OnKillableR" ), xorstr_( "[R] to Save Ally" ), true );
		Menu::AntiGapCloseR = RMenu->AddCheckBox( xorstr_( "AntiGapCloseR" ), xorstr_( "[R] to Anti-Gap Close" ), true );
		Menu::AOER = RMenu->AddSlider( xorstr_( "AOER" ), xorstr_( "[R] if hits >= #" ), 3, 0, 5, 1 );

		const auto Auto = Main->AddMenu( xorstr_( "AutoMenu" ), xorstr_( "- Automatic Settings -" ) );

		Menu::HeliaLogic = Auto->AddSlider( xorstr_( "HeliaLogic" ), xorstr_( "Auto Shield on X stacks of Echoes of Helia" ), 2, 1, 3, 1 );

		const auto CancelSpacer = Auto->AddInfo( xorstr_( "CancelSpacer" ), xorstr_( "Cancel Settings" ) );
		Menu::CancelW = Auto->AddCheckBox( xorstr_( "CancelW" ), xorstr_( "Use [Polymorph] to Cancel Spells" ), true );
		Menu::CancelWValue = Auto->AddSlider( xorstr_( "CancelWValue" ), xorstr_( "Minimum Importance for [W]" ), 1, 1, 3, 1 );

		Menu::CancelR = Auto->AddCheckBox( xorstr_( "CancelR" ), xorstr_( "Use [R] to Cancel Spells" ), true );
		Menu::CancelRValue = Auto->AddSlider( xorstr_( "CancelRValue" ), xorstr_( "Minimum Importance for [R]" ), 2, 1, 3, 1 );

		const auto CancelMenu = Auto->AddMenu( xorstr_( "CancelMenu" ), xorstr_( "Spells to Cancel:" ) );

		InitializeCancelMenu( CancelMenu );

		const auto BuffSpacer = Auto->AddInfo( xorstr_( "BuffSpacer" ), xorstr_( "Buffs to use [W/E] on:" ) );
		BuffSpacer->Hide( true );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( !Client->IsAlly() && GameStartData::GetInstance()->Map() != GameMapId::Arena )
				continue;

			switch ( Client->SkinHash() )
			{
			case SkinHash::Ashe:
				BuffSpacer->Hide( false );
				AsheQ.InitializeMenu( Auto, { 1 } );
				AsheQ.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_Q )->GetIcon() );
				TrackedBuffHashList.emplace_back( AsheQ );
				break;
			case SkinHash::Jinx:
				BuffSpacer->Hide( false );
				JinxP.InitializeMenu( Auto, { 0, 1 } );
				JinxP.MenuEntry->AddTexture( Client->GetPassiveIcon() );
				TrackedBuffHashList.emplace_back( JinxP );
				break;
			case SkinHash::Kaisa:
				BuffSpacer->Hide( false );
				KaisaE.InitializeMenu( Auto, { 0, 1 } );
				KaisaE.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_E )->GetIcon() );
				TrackedBuffHashList.emplace_back( KaisaE );
				break;
			case SkinHash::KogMaw:
				BuffSpacer->Hide( false );
				KogW.InitializeMenu( Auto, { 0, 1 } );
				KogW.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_W )->GetIcon() );
				TrackedBuffHashList.emplace_back( KogW );
				break;
			case SkinHash::Samira:
				BuffSpacer->Hide( false );
				SamiraR.InitializeMenu( Auto, {} );
				SamiraR.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_R )->GetIcon() );
				TrackedBuffHashList.emplace_back( SamiraR );
				break;
			case SkinHash::Twitch:
				BuffSpacer->Hide( false );
				TwitchQAS.InitializeMenu( Auto, { 1 } );
				TwitchQAS.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_Q )->GetIcon() );
				TrackedBuffHashList.emplace_back( TwitchQAS );
				TwitchR.InitializeMenu( Auto, { 0, 1 } );
				TwitchR.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_R )->GetIcon() );
				TrackedBuffHashList.emplace_back( TwitchR );
				break;
			case SkinHash::Vayne:
				BuffSpacer->Hide( false );
				VayneR.InitializeMenu( Auto, { 1 } );
				VayneR.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_R )->GetIcon() );
				TrackedBuffHashList.emplace_back( VayneR );
				break;
			case SkinHash::Xayah:
				BuffSpacer->Hide( false );
				XayahW.InitializeMenu( Auto, { 1 } );
				XayahW.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_W )->GetIcon() );
				TrackedBuffHashList.emplace_back( XayahW );
				break;
			case SkinHash::Zeri:
				BuffSpacer->Hide( false );
				ZeriR.InitializeMenu( Auto, { 0 } );
				ZeriR.MenuEntry->AddTexture( Client->GetSpell( Spell::kSpellSlot::SpellSlot_R )->GetIcon() );
				TrackedBuffHashList.emplace_back( ZeriR );
				break;
			default:
				break;
			}
		}

		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AA );

		EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );
		EventHandler::Add( Event::OnIterateHero, OnIterateChampions );
		EventHandler::Add( Event::OnBasicAttack, Spell_W::OnBasicAttack );

		OnChangeMenu();

		Globals::Print( xorstr_( "Loaded Blue Lulu" ) );
	}
}
