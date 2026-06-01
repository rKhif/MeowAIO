#include "Hwei.h"
#include "RiotSpellDatabase.h"
#include "BuffCache.h"
#include "MenuGUI.h"
#include "OrbwalkerSDK.h"
#include "Present.h"

namespace Features::Champions::Hwei
{
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

	bool IsCastingSpell()
	{
		return GlobalClocks::LastUpdateNetworkTime() <= CastEndTime;
	}

	bool CanCastSpell()
	{
		AIHeroClient* const Player = ObjectManager::GetPlayer();

		return !IsCastingSpell() && Player->GetSpellState( Spell::SpellSlot_Recall ) == Spell::kSpellState::SpellState_Ready && Player->mCharacterState()->CanMove();
	}

	std::unordered_set OnCCType = {
		BuffType::Knockup,
		BuffType::Knockback,
		BuffType::Asleep,
		BuffType::Taunt,
		BuffType::Snare,
		BuffType::Stun,
		BuffType::Suppression,
		BuffType::Flee,
		BuffType::Fear,
		BuffType::Charm
	};

	std::unordered_map<uint32_t, HweiSubject> SubjectResolver
	{
		{ HASH( "HweiQQ" ), HweiSubject::Disaster },
		{ HASH( "HweiWQ" ), HweiSubject::Serenity },
		{ HASH( "HweiEQ" ), HweiSubject::Torment },
		{ HASH( "HweiQ" ), HweiSubject::Subjectless }
	};

	static HweiSubject GetCurrentSubject()
	{
		AIHeroClient* const Player = ObjectManager::GetPlayer();
		return SubjectResolver[ Player->GetSpell( Spell::SpellSlot_Q )->Hash() ];
	}

	bool IsSubjectless()
	{
		return ObjectManager::GetPlayer()->GetSpell( Spell::SpellSlot_R )->Hash() == HASH( "HweiR" );
	}

	bool IsSpellActive( const HweiSpell Spell )
	{
		for ( const auto& Active : ActiveSpells | std::views::values )
		{
			if ( Active == Spell )
			{
				return true;
			}
		}

		return false;
	}

	void GoSubjectLess()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() - LastSubjectLessExitAttempt <= .5f )
		{
			return;
		}

		LastSubjectLessExitAttempt = GlobalClocks::LastUpdateNetworkTime();
		reinterpret_cast<char(__fastcall*)( HweiComponent*, std::uint32_t )>( DEFINE_RVA( Offsets::AIHeroClient::SetHweiSubject ) )( ObjectManager::GetPlayer()->GetHweiComponent(), Spell::SpellSlot_R );
	}

	static void CacheSubjects()
	{
		if ( !IsSubjectless() )
		{
			return;
		}

		AIHeroClient* const Player = ObjectManager::GetPlayer();

		Spell::SpellDataInst* SpellQ = Player->GetSpell( Spell::SpellSlot_Q );
		Spell::SpellDataInst* SpellW = Player->GetSpell( Spell::SpellSlot_W );
		Spell::SpellDataInst* SpellE = Player->GetSpell( Spell::SpellSlot_E );
		Spell::SpellDataInst* SpellR = Player->GetSpell( Spell::SpellSlot_R );

		if ( !SpellQ || !SpellW || !SpellE || !SpellR )
		{
			Globals::Print( xorstr_( "Spells are not found?!" ) );
			return;
		}

		SubjectLevel[ 0 ] = SpellQ->Level();
		SubjectLevel[ 1 ] = SpellW->Level();
		SubjectLevel[ 2 ] = SpellE->Level();
		LevelR = SpellR->Level();

		IsLearned[ 0 ] = SpellQ->HasLearned();
		IsLearned[ 1 ] = SpellW->HasLearned();
		IsLearned[ 2 ] = SpellE->HasLearned();
		IsLearned[ 3 ] = SpellR->HasLearned();

		SubjectCooldownEndTime[ 0 ] = SpellQ->IsReady() ? -1.f : SpellQ->CooldownEndTime();
		SubjectCooldownEndTime[ 1 ] = SpellW->IsReady() ? -1.f : SpellW->CooldownEndTime();
		SubjectCooldownEndTime[ 2 ] = SpellE->IsReady() ? -1.f : SpellE->CooldownEndTime();
		EndTimeR = SpellR->IsReady() ? -1.f : SpellR->CooldownEndTime();
	}

	float RadDifference( const Vector3f a, const Vector3f b )
	{
		const float DotProduct = a.x * b.x + a.z * b.z;
		const float Determinant = a.x * b.z - a.z * b.x;
		return atan2( Determinant, DotProduct );
	}

	float GetExplosionDamage( AIBaseClient* Client )
	{
		constexpr float Base[ 4 ] = { 0.f, 200.f, 300.f, 400.f };
		constexpr float Ratio = .8f;

		AIHeroClient* const Player = ObjectManager::GetPlayer();

		return Damage::CalculateMagicalDamage( Player, Client, Base[ LevelR ] + Ratio * Player->TotalAbilityPower() );
	}

	Vector3f RotateRad( Vector3f Vector, float Phi )
	{
		const auto C = cos( Phi );
		const auto S = sin( Phi );
		return { Vector.x * C + Vector.z * S, 0.f, Vector.z * C - Vector.x * S };
	}

	std::tuple<Vector3f, Vector3f, Vector3f, Vector3f> ComputeRectangle( const Vector3f PointA, const Vector3f PointB, const float Width )
	{
		const Vector3f Direction = Vector3f( PointB.x - PointA.x, 0.0f, PointB.z - PointA.z ).Normalized();

		const Vector3f Normal = { -Direction.z, 0.0f, Direction.x };

		const Vector3f HalfWidth = Normal * ( Width / 2.0f );

		Vector3f p1 = { PointA.x - HalfWidth.x, PointA.y, PointA.z - HalfWidth.z };
		Vector3f p2 = { PointA.x + HalfWidth.x, PointA.y, PointA.z + HalfWidth.z };
		Vector3f p3 = { PointB.x - HalfWidth.x, PointB.y, PointB.z - HalfWidth.z };
		Vector3f p4 = { PointB.x + HalfWidth.x, PointB.y, PointB.z + HalfWidth.z };

		return { p1, p2, p3, p4 };
	}

	void OnDrawWorld()
	{
		if ( Tracked.IsActive )
		{
			const DWORD ColorEdge = Graphics::RGBA( 136, 8, 8, 220 );
			auto [ p1, p2, p3, p4 ] = ComputeRectangle( Tracked.StartPos, Tracked.EndPos, QE.Radius * 2.f );

			Graphics::AddLine( p1, p2, 1, ColorEdge );
			Graphics::AddLine( p2, p4, 1, ColorEdge );
			Graphics::AddLine( p4, p3, 1, ColorEdge );
			Graphics::AddLine( p3, p1, 1, ColorEdge );
		}
	}

	struct Color
	{
		float R, G, B, A;
	};

	Color LerpColor( const Color& start, const Color& end, float t )
	{
		return { start.R + ( end.R - start.R ) * t, start.G + ( end.G - start.G ) * t, start.B + ( end.B - start.B ) * t, start.A + ( end.A - start.A ) * t };
	}

	void OnDraw()
	{
		if ( !WasFirstCastDone )
		{
			constexpr Color Start = { 196.f / 255.f, 30.f / 255.f, 58.f / 255.f, 1.0f }; // Red
			constexpr Color End = { 1.0f, 1.0f, 1.0f, 1.0f };                            // White

			const float t = fabs( sin( GlobalClocks::LastUpdateNetworkTime() ) );
			const Color CurrentValue = LerpColor( Start, End, t );
			const DWORD Value = Graphics::RGBA( CurrentValue.R * 255.f, CurrentValue.G * 255.f, CurrentValue.B * 255.f, CurrentValue.A * 255.f );

			const auto Text = xorstr_( "Please cast a spell manually!" );
			constexpr float FontSize = 32.f;
			const Vector2f DummySize = Graphics::TextSize( Text, FontSize, true );
			const ImVec2 ViewportSize = ImGui::GetMainViewport()->Size;

			Graphics::AddTextStroke( Text, FontSize, Vector2f{ ViewportSize.x / 2.f - DummySize.x / 2.f, 30.f }, Value, true );
		}

		if ( DebugMode->Value )
		{
			const Vector2f DummySize = Graphics::TextSize( xorstr_( "This is just for the console's size!" ), 12.f, false );
			Vector2f Cursor = { 30.f, 30.f };
			Graphics::AddRectangleFilled( Cursor, LastMaxWidth, ( DummySize.y + 1.f ) * HweiLogs.Size(), Graphics::RGBA( 30, 30, 30, 160 ) );
			Cursor.x += 5.f;
			Cursor.y += 1.f;

			const auto& Vector = HweiLogs.GetVector();

			if ( !Vector.empty() )
			{
				for ( int i = HweiLogs.Size() - 1; i >= 0; i-- )
				{
					char Buffer[ 128 ];
					sprintf_s( Buffer, sizeof Buffer, xorstr_( "[%i]: %s" ), i, Vector[ i ].c_str() );
					const std::string Constructed = Buffer;

					const Vector2f TextSize = Graphics::TextSize( Constructed.c_str(), 12.f, false );
					LastMaxWidth = max( LastMaxWidth, TextSize.x + 10.f );
					Graphics::AddText( Constructed.c_str(), 12.f, Cursor, Graphics::RGBA( 211, 211, 211, 255 ), false );
					Cursor.y += TextSize.y + 1.f;
				}
			}
		}
	}

	void DrawHweiHud()
	{
		for ( const auto& Enemy : ManagerTemplates::Heroes() )
		{
			BuffInstance* Instance = BuffCache::GetBuff( Enemy, HASH( "HweiRDespair" ) );

			if ( !Enemy->IsEnemy() || !Enemy->IsOnScreen() || !Enemy->IsVisible() || !Instance || Enemy->IsDead() || !BuffCache::HasBuff( Enemy, HASH( "HweiRDespair" ) ) || !Instance->IsActive() || Instance->RemainingTime() <= 0.f )
			{
				continue;
			}

			const float Damage = GetExplosionDamage( Enemy );
			const float RemainingHealth = Enemy->GetRealHealth( false, true ) - Damage;
			const bool IsKillable = RemainingHealth < 0.f;

			char Buffer[ 128 ];

			if ( IsKillable )
			{
				sprintf_s( Buffer, sizeof Buffer, xorstr_( "> Dead in %.1f <" ), Instance->RemainingTime() );
			}
			else
			{
				sprintf_s( Buffer, sizeof Buffer, xorstr_( "> Needs %.0f More Damage <" ), RemainingHealth );
			}

			std::string Text = Buffer;
			const Vector2f TextSize = Graphics::TextSize( Text.c_str(), 22.f, true );

			Vector2f ScreenPosition;

			if ( !r3dProjectToScreen( Enemy->Position(), ScreenPosition ) )
			{
				continue;
			}

			Graphics::AddTextStroke( Text.c_str(), 22.f, Vector2f{ ScreenPosition.x - TextSize.x / 2.f, ScreenPosition.y - 10.f }, IsKillable ? Graphics::RGBA( 0, 163, 108, 255 ) : Graphics::RGBA( 210, 4, 45, 255 ), true );
		}
	}

	void HweiPrint( const char* Format, ... )
	{
		va_list Args;
		va_start( Args, Format );

		va_list ArgsCopy;
		va_copy( ArgsCopy, Args );
		const int Size = vsnprintf( nullptr, 0, Format, ArgsCopy );
		va_end( ArgsCopy );

		std::string Result( Size, ' ' );
		if ( vsnprintf( Result.data(), Size + 1, Format, Args ) < S_OK )
		{
			return;
		}

		va_end( Args );
		HweiLogs.Push( Result );
	}

	HweiPriorities GetPriority( MenuSDK::List* Component )
	{
		return static_cast<HweiPriorities>( Component->Value );
	}

	float GetCost( const HweiSubject Subject )
	{
		constexpr float Disaster[ 7 ] = { 0.f, 80.f, 90.f, 100.f, 110.f, 120.f, 130.f };
		constexpr float Serenity[ 7 ] = { 0.f, 90.f, 95.f, 100.f, 105.f, 110.f, 115.f };
		constexpr float Torment[ 7 ] = { 0.f, 50.f, 55.f, 60.f, 65.f, 70.f, 75.f };

		switch ( Subject )
		{
		case HweiSubject::Disaster:
			return Disaster[ SubjectLevel[ 0 ] ];
		case HweiSubject::Serenity:
			return Serenity[ SubjectLevel[ 1 ] ];
		case HweiSubject::Torment:
			return Torment[ SubjectLevel[ 2 ] ];
		}

		return 0;
	}

	bool IsReady( const HweiSubject Subject )
	{
		const float LastUpdate = GlobalClocks::LastUpdateNetworkTime();

		switch ( Subject )
		{
		case HweiSubject::Disaster:
			return LastUpdate >= SubjectCooldownEndTime[ 0 ] && IsLearned[ 0 ];
		case HweiSubject::Serenity:
			return LastUpdate >= SubjectCooldownEndTime[ 1 ] && IsLearned[ 1 ];
		case HweiSubject::Torment:
			return LastUpdate >= SubjectCooldownEndTime[ 2 ] && IsLearned[ 2 ];
		default:
			break;
		}

		return false;
	}

	float GetDamageP( AIBaseClient* Client )
	{
		constexpr float Ratio = .3f;
		AIHeroClient* const Player = ObjectManager::GetPlayer();
		return Damage::CalculateMagicalDamage( Player, Client, 35.f + 145.f / 17.f * ( Player->mLevelRef() - 1 ) + Ratio * Player->TotalAbilityPower() );
	}

	float GetDamageR( AIBaseClient* Client )
	{
		constexpr float Base[ 4 ] = { 0.f, 200.f, 360.f, 490.f };
		constexpr float Ratio = .95f;

		AIHeroClient* const Player = ObjectManager::GetPlayer();

		return Damage::CalculateMagicalDamage( Player, Client, Base[ LevelR ] + Ratio * Player->TotalAbilityPower() );
	}

	bool SemiR()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() < EndTimeR || GlobalClocks::LastUpdateNetworkTime() < EECastTime + .8f || GlobalClocks::LastUpdateNetworkTime() < BlockRUntil )
		{
			return false;
		}

		if ( !IsLearned[ 3 ] )
		{
			return false;
		}

		AIHeroClient* const Player = ObjectManager::GetPlayer();
		const Vector3f PlayerPosition = Player->Position();
		const Vector3f CursorPosition = HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();

		const auto TargetFilter = [] ( AIBaseClient* Client ) { return SpiralingDespair::Whitelist.contains( Client->NetworkID() ) && SpiralingDespair::Whitelist[ Client->NetworkID() ]->Value; };

		auto Targets = TS->GetBestTargets( R, PlayerPosition, PlayerPosition, TargetFilter, SafetyCheck );

		const AIBaseClient* Best = nullptr;
		Prediction::PredictionResult BestPred{};
		float MinimumDistanceToCursor = 1000.f;

		for ( const auto& [ Target, Prediction ] : Targets )
		{
			const float LocalDistance = CursorPosition.Distance2D( Target->Position() );

			if ( LocalDistance > MinimumDistanceToCursor || Prediction.Hitchance < GetHitchance( SpiralingDespair::HitchanceSemiR ) )
			{
				continue;
			}

			MinimumDistanceToCursor = LocalDistance;
			BestPred = Prediction;
			Best = Target;
		}

		if ( !Best )
		{
			return false;
		}

		if ( !IsSubjectless() )
		{
			GoSubjectLess();
			HweiPrint( xorstr_( "Exiting Subject for Semi R" ) );
		}

		if ( R.CastSpell( BestPred.CastPosition ) )
		{
			HweiPrint( xorstr_( "Casted R! Pausing Torment usage for %.2fs" ), BestPred.TimeToTarget + .1f );
			BlockUntil[ HweiSubject::Torment ] = GlobalClocks::LastUpdateNetworkTime() + BestPred.TimeToTarget + .1f;
			return true;
		}

		return false;
	}

	bool HandleFollowUp()
	{
		LastCastAttempt = GlobalClocks::LastUpdateNetworkTime();
		AIHeroClient* const Player = ObjectManager::GetPlayer();
		const Vector3f PlayerPosition = Player->Position();

		//
		// Maw already crushed / Noone in it anymore
		//
		if ( GlobalClocks::LastUpdateNetworkTime() >= CrushingMaw.CastTime + EE.CastDelay || !Torment::IsInCross( CrushingMaw.CastPosition, CrushingMaw.Target->Position() ) || !CrushingMaw.Target )
		{
			CrushingMaw.IsActive = false;
			return false;
		}

		switch ( CrushingMaw.SetupAbility )
		{
		case 0:
			return false;
		case 1:
			{
				if ( !IsReady( HweiSubject::Disaster ) || CrushingMaw.CastPosition.Distance2D( PlayerPosition ) > QQ.Range )
				{
					return false;
				}

				std::vector<Prediction::CollidedObject> Objects;
				if ( PredictionSuccess( FindSpellCollisions( QQ, PlayerPosition, PlayerPosition, CrushingMaw.CastPosition, CrushingMaw.Target, &Objects ) ) && Objects.empty() )
				{
					CrushingMaw.CastAttempts++;
					if ( Player->CastSpellHwei( Spell::SpellSlot_Q, Spell::SpellSlot_Q, CrushingMaw.CastPosition, true ) )
					{
						if ( CrushingMaw.CastAttempts == 3 )
						{
							CrushingMaw.IsActive = false; // So we don't spam Cast Attempts
						}
						return true;
					}
				}
			}
			break;
		case 2:
			{
				if ( EndTimeR > GlobalClocks::LastUpdateNetworkTime() || !IsLearned[ 3 ] )
				{
					CrushingMaw.SetupAbility = 1;
					return false;
				}

				const float TimeToReach = R.CastDelay + PlayerPosition.Distance2D( CrushingMaw.CastPosition ) / R.Speed;

				if ( GlobalClocks::LastUpdateNetworkTime() + TimeToReach < CrushingMaw.CastTime + EE.CastDelay )
				{
					Globals::Print( xorstr_( "Waiting on R" ) );
					// Await
					return true;
				}

				if ( !IsSubjectless() )
				{
					GoSubjectLess();
				}

				CrushingMaw.CastAttempts++;
				Globals::Print( xorstr_( "Attempt" ) );
				if ( Player->CastSpell( Spell::SpellSlot_R, CrushingMaw.CastPosition, true ) )
				{
					if ( CrushingMaw.CastAttempts == 3 )
					{
						CrushingMaw.IsActive = false; // So we don't spam Cast Attempts
					}
					return true;
				}
			}
			break;
		default:
			break;
		}

		return false;
	}

	float HealthAfterFullCombo( AIBaseClient* Target, bool IncludeQ, bool IncludeW, bool IncludeE, bool IncludeR, float AAs )
	{
		AIHeroClient* const Player = ObjectManager::GetPlayer();

		float ToReturn = Target->GetRealHealth( false, true );

		if ( IncludeE )
		{
			ToReturn -= Torment::GetDamage( Target );
		}

		if ( IncludeW )
		{
			ToReturn -= Serenity::GetDamageE( Target );
		}

		if ( IncludeR )
		{
			ToReturn -= GetDamageR( Target );
		}

		ToReturn -= Damage::CalculateAutoAttackDamage( Player, Target, true ) * AAs;
		ToReturn -= GetDamageP( Target ) * 2.f;

		if ( IncludeQ )
		{
			const float DamageQQ = Disaster::GetDamageQ( Target );
			const float DamageQW = Disaster::GetDamageW( Target, ToReturn );

			ToReturn -= max( DamageQW, DamageQQ );
		}

		return ToReturn;
	}

	bool QueryCC()
	{
		AIHeroClient* const Player = ObjectManager::GetPlayer();
		const Vector3f PlayerPosition = Player->Position();

		LastCastAttempt = GlobalClocks::LastUpdateNetworkTime();

		const auto TargetFilter = [] ( AIBaseClient* Client ) { return SpiralingDespair::Whitelist.contains( Client->NetworkID() ) && SpiralingDespair::Whitelist[ Client->NetworkID() ]->Value; };
		const auto PredictionFilter = [] ( AIBaseClient* Client, Prediction::PredictionStatus Status, Prediction::PredictionResult* Result ) { return PredictionSuccess( Status ) && Result->Hitchance > Prediction::VeryHigh; };

		for ( const auto& [ Target, Prediction ] : TS->GetBestTargets( QQ, PlayerPosition, PlayerPosition, nullptr, PredictionFilter ) )
		{
			if ( !IsReady( HweiSubject::Disaster ) || !Disaster::ComboQ->Value )
			{
				break;
			}

			const bool IsKillableWithQQ = HealthAfterFullCombo( Target, true, true, false, false ) <= 0.f;

			if ( IsKillableWithQQ )
			{
				return Player->CastSpellHwei( Spell::SpellSlot_Q, Spell::SpellSlot_Q, Prediction.CastPosition, true );
			}
		}

		if ( !SpiralingDespair::ComboR->Value || EndTimeR > GlobalClocks::LastUpdateNetworkTime() || !IsLearned[ 3 ] )
		{
			return false;
		}

		for ( const auto& [ Target, Prediction ] : TS->GetBestTargets( R, PlayerPosition, PlayerPosition, TargetFilter, PredictionFilter ) )
		{
			const float AllowedDespairCooldown = Prediction.TimeToTarget + 1.8f;
			const bool CountDespair = IsReady( HweiSubject::Disaster ) || GlobalClocks::LastUpdateNetworkTime() + AllowedDespairCooldown >= SubjectCooldownEndTime[ 0 ];

			const bool IsOverkill = HealthAfterFullCombo( Target, CountDespair, true, false, false, 2 ) <= 0.f;
			const bool IsKillableWithR = HealthAfterFullCombo( Target, CountDespair, true, false, true ) <= 0.f;

			if ( IsKillableWithR && !IsOverkill )
			{
				if ( !IsSubjectless() )
				{
					GoSubjectLess();
					HweiPrint( xorstr_( "Exiting Subject for R Combo" ) );
				}

				return R.CastSpell( Prediction.CastPosition, true, true );
			}
		}

		return false;
	}

	bool QueryR()
	{
		if ( GlobalClocks::LastUpdateNetworkTime() < EndTimeR || GlobalClocks::LastUpdateNetworkTime() < EECastTime + .8f || GlobalClocks::LastUpdateNetworkTime() < BlockRUntil )
		{
			return false;
		}

		if ( !IsLearned[ 3 ] )
		{
			return false;
		}

		AIHeroClient* const Player = ObjectManager::GetPlayer();
		const Vector3f PlayerPosition = Player->Position();

		float NecessaryMana = 100.f;

		NecessaryMana += ( IsReady( HweiSubject::Torment ) ) ? GetCost( HweiSubject::Torment ) : 0.f;
		NecessaryMana += ( IsReady( HweiSubject::Disaster ) ) ? GetCost( HweiSubject::Disaster ) : 0.f;

		if ( Player->mResource()->mPAR.Value - NecessaryMana < 0.f )
		{
			return false;
		}

		LastCastAttempt = GlobalClocks::LastUpdateNetworkTime();

		const auto TargetFilter = [] ( AIBaseClient* Client ) { return SpiralingDespair::Whitelist.contains( Client->NetworkID() ) && SpiralingDespair::Whitelist[ Client->NetworkID() ]->Value; };

		const auto Targets = TS->GetBestTargets( R, PlayerPosition, PlayerPosition, TargetFilter );

		for ( const auto& [ Target, Prediction ] : Targets )
		{
			if ( Prediction.Hitchance < GetHitchance( SpiralingDespair::HitchanceR ) )
			{
				continue;
			}

			const float DamageQQ = Disaster::GetDamageQ( Target );
			const float DamageWQ = Disaster::GetDamageW( Target );

			uint8_t Iterations = 1;
			Iterations += IsReady( HweiSubject::Disaster ) ? 1 : 0;
			Iterations += IsReady( HweiSubject::Serenity ) ? 1 : 0;
			Iterations += IsReady( HweiSubject::Torment ) ? 1 : 0;

			const float DamageP = Iterations >= 2 ? GetDamageP( Target ) : 0.f;
			const float DamageQ = IsReady( HweiSubject::Disaster ) ? max( DamageQQ, DamageWQ ) : 0.f;
			const float DamageW = IsReady( HweiSubject::Serenity ) ? Serenity::GetDamageE( Target ) : 0.f;
			const float DamageE = IsReady( HweiSubject::Torment ) ? Torment::GetDamage( Target ) : 0.f;
			const float DamageR = GetDamageR( Target );

			float CachedDamage = Damage::GetIncomingDamage( Target, Prediction.TimeToTarget );

			for ( const auto& ActiveSpell : ActiveSpells | std::views::values )
			{
				switch ( ActiveSpell )
				{
				case HweiSpell::QQ:
					CachedDamage += DamageQQ;
					break;
				case HweiSpell::QW:
					CachedDamage += DamageWQ;
					break;
				case HweiSpell::EQ:
				case HweiSpell::EE:
					CachedDamage += Torment::GetDamage( Target );
					break;
				default:
					break;
				}
			}

			if ( Target->GetRealHealth() - CachedDamage < Target->GetMaxHealth() * .12f )
			{
				continue;
			}

			const float Additional = 2.f * Damage::CalculateAutoAttackDamage( Player, Target, true ) + Iterations / 2 * DamageP; // TODO: Add Luden's etc.

			const float Total = DamageQ + DamageW + DamageE + DamageR + Additional;
			const bool IsOverkill = DamageQ + DamageW + DamageE + DamageP + ( Damage::CalculateAutoAttackDamage( Player, Target, true ) * 3.f ) > Target->GetRealHealth( false, true ) - CachedDamage;

			if ( Total > Target->GetRealHealth( false, true ) && !IsOverkill )
			{
				HweiPrint( xorstr_( "Initiating Kill Combo on %s for %.2f Damage" ), GetDisplayName( Target ), Total );

				if ( !IsSubjectless() )
				{
					GoSubjectLess();
					HweiPrint( xorstr_( "Exiting Subject for R Combo" ) );
				}

				if ( R.CastSpell( Prediction.CastPosition ) )
				{
					return true;
				}
			}
		}

		return false;
	}

	void Combo()
	{
		if ( CrushingMaw.IsActive && CrushingMaw.SetupAbility != 0 )
		{
			if ( HandleFollowUp() )
				return;
		}

		if ( GlobalClocks::LastUpdateNetworkTime() - LastCastAttempt <= .25f )
		{
			return;
		}

		QueryCC();

		if ( QueryR() )
		{
			return;
		}

		if ( IsReady( HweiSubject::Torment ) && GlobalClocks::LastUpdateNetworkTime() > BlockUntil[ HweiSubject::Torment ] && IsLearned[ 2 ] )
		{
			Torment::QueryE();
			Torment::QueryW();
			Torment::QueryQ();
		}

		if ( IsReady( HweiSubject::Disaster ) && GlobalClocks::LastUpdateNetworkTime() > BlockUntil[ HweiSubject::Disaster ] && IsLearned[ 0 ] && !IsSpellActive( HweiSpell::EE ) && GlobalClocks::LastUpdateNetworkTime() >= EECastTime + .8f )
		{
			Disaster::QueryW();
			Disaster::QueryQ();
			Disaster::QueryE();
		}

		if ( IsReady( HweiSubject::Serenity ) )
		{
		}

		if ( BestToCast.Priority != HweiPriorities::Zero && BestToCast.Subject != HweiSubject::Subjectless )
		{
			LastCastAttempt = GlobalClocks::LastUpdateNetworkTime();
			Serenity::OnHweiSpellCast( BestToCast.Target, BestToCast.Subject, BestToCast.Slot );

			if ( BestToCast.Process )
			{
				Spell::kSpellSlot CastSlot;

				switch ( BestToCast.Subject )
				{
				case HweiSubject::Disaster:
					CastSlot = Spell::SpellSlot_Q;
					break;
				case HweiSubject::Serenity:
					CastSlot = Spell::SpellSlot_W;
					break;
				case HweiSubject::Torment:
					CastSlot = Spell::SpellSlot_E;
					break;
				default:
					return;
				}

				if ( !CanCastSpell() )
				{
					return;
				}

				bool IsPassed = false;

				AIHeroClient* const Player = ObjectManager::GetPlayer();

				switch ( BestToCast.CastType )
				{
				case HweiCastType::SinglePred:
					if ( BestToCast.PredResult.CastPosition.IsNonZeroValid() )
					{
						IsPassed = Player->CastSpellHwei( CastSlot, BestToCast.Slot, BestToCast.PredResult.CastPosition, false );
					}
					break;
				case HweiCastType::MultiPred:
					if ( BestToCast.MultiPredResult.CastPosition.IsNonZeroValid() )
					{
						IsPassed = Player->CastSpellHwei( CastSlot, BestToCast.Slot, BestToCast.MultiPredResult.CastPosition, false );
					}
					break;
				case HweiCastType::CastPosition:
					if ( BestToCast.CastOverride.IsNonZeroValid() )
					{
						IsPassed = Player->CastSpellHwei( CastSlot, BestToCast.Slot, BestToCast.CastOverride, false );
					}
					break;
				case HweiCastType::Target: // There's no actual targeted spells, that's basically W-E (Stirring Lights) and it's an auto-cast on self
					IsPassed = Player->CastSpellHwei( CastSlot, BestToCast.Slot, false );
					break;
				default:
					BestToCast = { .Priority = HweiPriorities::Zero, .Subject = HweiSubject::Subjectless, .Slot = Spell::SpellSlot_Unknown, .CastType = HweiCastType::None };
					return;
				}

				if ( IsPassed )
				{
					if ( CastSlot == Spell::SpellSlot_E && BestToCast.Slot == Spell::SpellSlot_E )
					{
						HweiPrint( xorstr_( "Casted EE at %s" ), BestToCast.CastOverride.ToString().c_str() );
						EEWasCasted = true;
						EECastTime = GlobalClocks::LastUpdateNetworkTime();
						BestToCast = { .Priority = HweiPriorities::Zero, .Subject = HweiSubject::Subjectless, .Slot = Spell::SpellSlot_Unknown, .CastType = HweiCastType::None };
						Orbwalker::SDK::SetAttackPause( .7f );
						return;
					}

					HweiPrint( xorstr_( "Succesfully Casted %s on %s" ), SpellSlotToString[ BestToCast.Slot ].c_str(), BestToCast.Target ? BestToCast.Target->SkinName() : xorstr_( "nullptr" ) );
				}
				else
				{
					HweiPrint( xorstr_( "Failed to Cast %s (Post Subject Swap to %s)" ), SpellSlotToString[ BestToCast.Slot ].c_str(), BestToCast.Target ? BestToCast.Target->SkinName() : xorstr_( "nullptr" ) );
				}

				BestToCast = { .Priority = HweiPriorities::Zero, .Subject = HweiSubject::Subjectless, .Slot = Spell::SpellSlot_Unknown, .CastType = HweiCastType::None };
				return;
			}
		}

		BestToCast = { .Priority = HweiPriorities::Zero, .Subject = HweiSubject::Subjectless, .Slot = Spell::SpellSlot_Unknown, .CastType = HweiCastType::None };
	}

	int i = 0;
	float LastPing = 0.f;

	void HandleSpells()
	{
		if ( ObjectManager::GetPlayer()->IsDead() )
		{
			return;
		}

		if ( SpiralingDespair::SemiR->Value && SemiR() )
		{
			return;
		}

		switch ( Orbwalker::ActiveMode )
		{
		case Orbwalker::Mode::Combo:
			Combo();
			break;
		case Orbwalker::Mode::Harass:
			Disaster::QueryQ( false );
			break;
		case Orbwalker::Mode::Laneclear:
			Disaster::QueryLaneClearE();
			break;
		default:
			break;
		}

		Disaster::QueryW( false );
	}

	void OnGameUpdate()
	{
		HweiActions.Update();
		if ( Tracked.IsActive && GlobalClocks::LastUpdateNetworkTime() > Tracked.CreateTime + .35f + QE.Range / QE.Speed + 2.5f )
		{
			Tracked.IsActive = false;
		}

		Serenity::QueryW();

		LocalPosition = ObjectManager::GetPlayer()->Position();
		CacheSubjects();
		HandleSpells();
	}

	void OnCreate( GameObject* Object )
	{
		const std::string Name = Object->Name();

		if ( Name.size() < 5 || Object->IsEnemy() )
		{
			return;
		}

		if ( Name[ 0 ] != 'H' || Name[ 1 ] != 'w' || Name[ 2 ] != 'e' || Name[ 3 ] != 'i' )
		{
			return;
		}

		Globals::Print( xorstr_( "Created %s" ), Name.c_str() );

		if ( Name.contains( xorstr_( "_Q_Q_Mis" ) ) )
		{
			ActiveSpells[ Object->Handle() ] = HweiSpell::QQ;
			return;
		}

		if ( Name.contains( xorstr_( "_Q_W_warning" ) ) )
		{
			ActiveSpells[ Object->Handle() ] = HweiSpell::QW;
			return;
		}

		if ( Name.contains( xorstr_( "_E_Q_Mis" ) ) )
		{
			ActiveSpells[ Object->Handle() ] = HweiSpell::EQ;
			return;
		}

		if ( Name.contains( xorstr_( "_E_Shared_Cas" ) ) )
		{
			ActiveSpells[ Object->Handle() ] = HweiSpell::EE;
			return;
		}
	}

	void OnDelete( GameObject* Object )
	{
		ActiveSpells.erase( Object->Handle() );
	}

	void CacheTextures()
	{
		Spell::Database::SpellDatabase* const Database = Spell::Database::SpellDatabase::GetInstance();
		constexpr uint32_t SpellHashes[ 10 ] = { SPELL_HASH( "HweiQQ" ), SPELL_HASH( "HweiQW" ), SPELL_HASH( "HweiQE" ), SPELL_HASH( "HweiWQ" ), SPELL_HASH( "HweiWW" ), SPELL_HASH( "HweiWE" ), SPELL_HASH( "HweiEQ" ), SPELL_HASH( "HweiEW" ), SPELL_HASH( "HweiEE" ), SPELL_HASH( "HweiR" ) };
		constexpr uint32_t SubjectHashes[ 3 ] = { SPELL_HASH( "HweiQ" ), SPELL_HASH( "HweiW" ), SPELL_HASH( "HweiE" ) };

		uint8_t Count = 0;

		for ( uint8_t i = 0; i < 10; i++ )
		{
			Spell::SpellData* const SpellInstance = Database->Get( SpellHashes[ i ] );

			if ( SpellInstance )
			{
				SpellTextures[ static_cast<HweiSpell>( i ) ] = SpellInstance->GetTexture();
				Count++;
			}
		}

		for ( uint8_t i = 0; i < 3; i++ )
		{
			Spell::SpellData* const SpellInstance = Database->Get( SubjectHashes[ i ] );

			if ( SpellInstance )
			{
				SubjectTextures[ static_cast<HweiSubject>( i + 1 ) ] = SpellInstance->GetTexture();
				Count++;
			}
		}

		Globals::Print( xorstr_( "Pushed Back %u / 13 Textures" ), Count );
	}

	void OnProcessSpell( AIBaseClient* Sender, Spell::SpellCastInfo* Spell )
	{
		if ( Sender->SkinHash() != SkinHash::Hwei || !Sender->IsMe() )
		{
			return;
		}

		WasFirstCastDone = true;

		switch ( Spell->GetNameHash() )
		{
		case HASH( "HweiQQ" ):
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .26f;
			break;
		case HASH( "HweiQW" ):
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .51f;
			break;
		case HASH( "HweiQE" ):
			Tracked.IsActive = true;
			Tracked.StartPos = Spell->StartPosition();
			Tracked.EndPos = Spell->StartPosition().Extend( Spell->EndPosition(), QE.Range );
			Tracked.CreateTime = GlobalClocks::LastUpdateNetworkTime();
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .36f;
			break;
		case HASH( "HweiWQ" ):
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .26f;
			break;
		case HASH( "HweiWW" ):
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .31f;
			break;
		case HASH( "HweiEQ" ):
		case HASH( "HweiEW" ):
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .26f;
			break;
		case HASH( "HweiEE" ):
			CrushingMaw.IsActive = true;
			CrushingMaw.CastTime = GlobalClocks::LastUpdateNetworkTime();
			break;
		case HASH( "HweiR" ):
			CastEndTime = GlobalClocks::LastUpdateNetworkTime() + .26f;
			Serenity::OnHweiSpellCast( nullptr, HweiSubject::Subjectless, Spell::SpellSlot_R );
			break;
		}
	}

	void OnOrbwalkerAction( const Orbwalker::ActionArgs& Args, bool* Process )
	{
		if ( Args.Type == Orbwalker::ActionArgs::After && Args.Target )
		{
			Serenity::OnHweiSpellCast( Args.Target, HweiSubject::Subjectless, Spell::SpellSlot_BasicAttack );
		}
	}

	void Initialize()
	{
		TS = TargetSelector::GetInstance( TargetSelector::DamageModes::AP );

		QQ = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 875.f + 175.f,
			.RangeType = Edge,
			.Speed = 2000.f,
			.CalculateTimeTraveledCustom = [] ( const BaseSpell& Spell, const float Distance )
			{
				return Spell.CastDelay + ( Distance <= 875.f ? Distance / Spell.Speed : ( 875.f / Spell.Speed ) );
			},
			.Radius = 70.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall, // TODO ?
			.ExtraCollisionRadius = 20.f
		} );

		QW = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_W,
			.Type = Circular,
			.CastDelay = 1.f,
			.Range = 1900.f,
			.RangeType = Center,
			.Speed = 0.f,
			.Radius = 200.f
		} );

		QE = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_E,
			.Type = Linear,
			.CastDelay = .35f,
			.Range = 1250.f,
			.RangeType = Center,
			.Speed = 1500.f,
			.Radius = 220.f,
			.HeroCollisionType = Soft,
			.MinionCollisionType = Soft
		} );

		WQ = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 1200.f,
			.RangeType = Center,
			.Speed = 1700.f,
			.Radius = 145.f,
			.HeroCollisionType = SpellCollisionType::None,
			.MinionCollisionType = SpellCollisionType::None,
			.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall, // TODO ?
		} );

		WW = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_W,
			.Type = Circular,
			.CastDelay = .3f,
			.Range = 650.f,
			.RangeType = Center,
			.Speed = 0.f,
			.Radius = 350.f,
			.HeroCollisionType = SpellCollisionType::None,
			.MinionCollisionType = SpellCollisionType::None,
		} );

		WE = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_E,
			.Type = Self
		} );

		EQ = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_Q,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 1100.f,
			.RangeType = Edge,
			.Speed = 1300.f,
			.Radius = 70.f,
			.HeroCollisionType = Hard,
			.MinionCollisionType = Hard,
			.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall,
			.ExtraCollisionRadius = 10.f
		} );

		EW = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_W,
			.Type = Linear,
			.CastDelay = .3f + .65f,
			.Range = 900.f,
			.RangeType = Center,
			.Speed = 1700.f,
			.Radius = 350.f,
			.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall
		} );

		EE = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_E,
			.Type = Circular,
			.CastDelay = .627f,
			.Range = 1100.f,
			.RangeType = Center,
			.Speed = 0.f,
			.Radius = 80.f
		} );

		R = BaseSpell( BaseSpellData{
			.Slot = Spell::SpellSlot_R,
			.Type = Linear,
			.CastDelay = .25f,
			.Range = 1300.f,
			.RangeType = Edge,
			.Speed = 1400.f,
			.Radius = 90.f,
			.HeroCollisionType = Hard,
			.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall
		} );

		CacheTextures();

		AIHeroClient* const Player = ObjectManager::GetPlayer();

		const auto MainMenu = MenuSDK::Menu::CreateMenu( xorstr_( "BlueHwei" ), xorstr_( "Blue Hwei" ) );
		MainMenu->AddTexture( Player->GetIconSquare() );

		const auto MenuP = MainMenu->AddMenu( xorstr_( "MenuP" ), xorstr_( "[P / Signature of the Visionary] Settings" ) );
		MenuP->AddTexture( Player->GetPassiveIcon() );
		DebugMode = MenuP->AddCheckBox( xorstr_( "DebugMode" ), xorstr_( "Debug Mode" ), true );

		const auto MenuD = MainMenu->AddMenu( xorstr_( "DisasterMenu" ), xorstr_( "[Disaster] Subject Settings" ) );
		MenuD->AddTexture( SubjectTextures[ HweiSubject::Disaster ] );

		const auto MenuQQ = MenuD->AddMenu( xorstr_( "MenuQQ" ), xorstr_( "[Q / Devastating Fire] Settings" ) );
		MenuQQ->AddTexture( SpellTextures[ HweiSpell::QQ ] );
		Disaster::ComboQ = MenuQQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
		Disaster::HarassQ = MenuQQ->AddCheckBox( xorstr_( "HarassQ" ), xorstr_( "Use in Harass" ), true );
		Disaster::CollisionQ = MenuQQ->AddCheckBox( xorstr_( "CollisionQQ" ), xorstr_( "Allow QQ on Minions for AOE QQ" ), true );
		Disaster::PriorityQ = MenuQQ->AddList( xorstr_( "PriorityQQ" ), xorstr_( "Spell Priority" ), HweiStringPriorities, 3 );

		const auto MenuQW = MenuD->AddMenu( xorstr_( "MenuQW" ), xorstr_( "[W / Severing Bolt] Settings" ) );
		MenuQW->AddTexture( SpellTextures[ HweiSpell::QW ] );
		Disaster::ComboW = MenuQW->AddCheckBox( xorstr_( "ComboW" ), xorstr_( "Use in Combo" ), true );
		Disaster::SemiW = MenuQW->AddKeyBind( xorstr_( "SemiQW" ), xorstr_( "Semi Key" ), VK_F1, false, false );
		Disaster::SemiWMode = MenuQW->AddList( xorstr_( "SemiQWMode" ), xorstr_( "Semi W Mode" ), { xorstr_( "Hold" ), xorstr_( "Toggle" ) }, 0, [&MenuQW] ( MenuSDK::List*, unsigned Value ) { Disaster::SemiW->IsToggle = Value == 1; } );
		Disaster::SemiWMode->Callback( nullptr, Disaster::SemiWMode->Value );
		MenuSDK::PermaShow::AddComponent( xorstr_( "Semi QW" ), Disaster::SemiW );
		Disaster::AutoRQW = MenuQW->AddCheckBox( xorstr_( "AutoRQW" ), xorstr_( "Always Swap to Disaster if W is Guaranteed" ), true );
		Disaster::AutoRQW->AddTexture( SubjectTextures[ HweiSubject::Disaster ] );
		Disaster::PriorityW = MenuQW->AddList( xorstr_( "PriorityQW" ), xorstr_( "Spell Priority" ), HweiStringPriorities, 3 );

		const auto MenuQE = MenuD->AddMenu( xorstr_( "MenuQE" ), xorstr_( "[E / Molten Fissure] Settings" ) );
		MenuQE->AddTexture( SpellTextures[ HweiSpell::QE ] );
		Disaster::ComboE = MenuQE->AddCheckBox( xorstr_( "ComboE" ), xorstr_( "Use in Combo" ), false );
		Disaster::UsageE = MenuQE->AddMultiList( xorstr_( "UsageQE" ), xorstr_( "Usage" ), { xorstr_( "AOE" ), xorstr_( "Getting Chased" ) }, { 0, 1 } );
		Disaster::PriorityE = MenuQE->AddList( xorstr_( "PriorityQE" ), xorstr_( "Spell Priority" ), HweiStringPriorities, 2 );
		std::unordered_map<int, std::string> LaneClearStrings = { { 0, xorstr_( "Disabled" ) } };
		Disaster::LaneClearE = MenuQE->AddSlider( xorstr_( "LaneClearE" ), xorstr_( "Amount of Minions" ), LaneClearStrings, 7, 0, 12, 1 );

		const auto MenuS = MainMenu->AddMenu( xorstr_( "SerenityMenu" ), xorstr_( "[Serenity] Subject Settings" ) );
		MenuS->AddTexture( SubjectTextures[ HweiSubject::Serenity ] );

		const auto MenuWQ = MenuS->AddMenu( xorstr_( "MenuWQ" ), xorstr_( "[Q / Fleeting Current] Settings" ) );
		MenuWQ->AddTexture( SpellTextures[ HweiSpell::WQ ] );
		Serenity::AllowQ = MenuWQ->AddCheckBox( xorstr_( "AllowWQ" ), xorstr_( "Allow Usage" ), true );
		Serenity::OnGapCloseQ = MenuWQ->AddCheckBox( xorstr_( "OnGapCloseQ" ), xorstr_( "Auto Away on Gap Close" ), true );

		const auto MenuWW = MenuS->AddMenu( xorstr_( "MenuWW" ), xorstr_( "[W / Pool of Reflection] Settings" ) );
		MenuWW->AddTexture( SpellTextures[ HweiSpell::WW ] );
		Serenity::AllowW = MenuWW->AddCheckBox( xorstr_( "AllowWW" ), xorstr_( "Allow Usage" ), false );
		Serenity::UsageWW = MenuWW->AddMultiList( xorstr_( "UsageWW" ), xorstr_( "Usage" ), { xorstr_( "Lethal" ), xorstr_( "Huge Damage" ), xorstr_( "Damage > Shield Value" ) }, { 0 } );
		Serenity::TargetWW = MenuWW->AddMultiList( xorstr_( "TargetWW" ), xorstr_( "Targets" ), { xorstr_( "Self" ), xorstr_( "Allies" ) }, { 0 } );

		const auto MenuWE = MenuS->AddMenu( xorstr_( "MenuWE" ), xorstr_( "[E / Stirring Lights] Settings" ) );
		MenuWE->AddTexture( SpellTextures[ HweiSpell::WE ] );
		Serenity::AllowE = MenuWE->AddCheckBox( xorstr_( "AllowWE" ), xorstr_( "Allow Usage" ), true );
		Serenity::AutoEMode = MenuWE->AddMultiList( xorstr_( "OrbwalkerModes" ), xorstr_( "Orbwalker Modes" ), { xorstr_( "Combo" ), xorstr_( "Harass" ), xorstr_( "Lane Clear" ), xorstr_( "Always" ) }, { 0, 2 } );
		Serenity::AutoESpells = MenuWE->AddMultiList( xorstr_( "AutoSpells" ), xorstr_( "On Before Spells" ), { xorstr_( "AA" ), xorstr_( "QQ" ), xorstr_( "QW" ), xorstr_( "QE" ), xorstr_( "EQ" ), xorstr_( "EE" ), xorstr_( "R" ) }, { 1, 2, 3, 4, 6 } );

		const auto MenuT = MainMenu->AddMenu( xorstr_( "TormentMenu" ), xorstr_( "[Torment] Subject Settings" ) );
		MenuT->AddTexture( SubjectTextures[ HweiSubject::Torment ] );

		const auto MenuEQ = MenuT->AddMenu( xorstr_( "MenuEQ" ), xorstr_( "[Q / Grim Visage] Settings" ) );
		MenuEQ->AddTexture( SpellTextures[ HweiSpell::EQ ] );
		Torment::ComboQ = MenuEQ->AddCheckBox( xorstr_( "ComboQ" ), xorstr_( "Use in Combo" ), true );
		Torment::SpamQ = MenuEQ->AddCheckBox( xorstr_( "SpamQ" ), xorstr_( "Spam Usage" ), false );
		Torment::PriorityQ = MenuEQ->AddList( xorstr_( "PriorityEQ" ), xorstr_( "Spell Priority" ), HweiStringPriorities, 4 );
		Torment::ContinueCCQ = MenuEQ->AddCheckBox( xorstr_( "ContinueCCQ" ), xorstr_( "Use to Continue CC" ), true );
		Torment::AntiGapCloseQ = MenuEQ->AddCheckBox( xorstr_( "AntiGapCloseQ" ), xorstr_( "Anti-Gap Close" ), true );

		const auto MenuEW = MenuT->AddMenu( xorstr_( "MenuEW" ), xorstr_( "[W / Gaze of the Abyss] Settings" ) );
		MenuEW->AddTexture( SpellTextures[ HweiSpell::EW ] );
		Torment::ComboW = MenuEW->AddCheckBox( xorstr_( "ComboW" ), xorstr_( "Use in Combo" ), true );
		Torment::PriorityW = MenuEW->AddList( xorstr_( "PriorityEW" ), xorstr_( "Spell Priority" ), HweiStringPriorities, 4 );
		Torment::ContinueCCW = MenuEW->AddCheckBox( xorstr_( "ContinueCCW" ), xorstr_( "Use to Continue CC" ), true );
		Torment::NonGuaranteedW = MenuEW->AddCheckBox( xorstr_( "NonGEW" ), xorstr_( "Use if Non-Guaranteed" ), true );
		Torment::AntiGapCloseW = MenuEW->AddCheckBox( xorstr_( "AntiGapCloseW" ), xorstr_( "Anti-Gap Close" ), true );
		const auto SpecialSpells = MenuEW->AddMenu( xorstr_( "SpecialEW" ), xorstr_( "Special Spells" ) );

		const auto MenuEE = MenuT->AddMenu( xorstr_( "MenuEE" ), xorstr_( "[E / Crushing Maw] Settings" ) );
		MenuEE->AddTexture( SpellTextures[ HweiSpell::EE ] );
		Torment::ComboE = MenuEE->AddCheckBox( xorstr_( "ComboE" ), xorstr_( "Use in Combo" ), true );
		Torment::PriorityE = MenuEE->AddList( xorstr_( "PriorityEE1" ), xorstr_( "Spell Priority" ), HweiStringPriorities, 5 );
		Torment::AOE_E = MenuEE->AddCheckBox( xorstr_( "AOE_E" ), xorstr_( "Attempt AOE E" ), true );
		Torment::AllowSetupEQ = MenuEE->AddList( xorstr_( "SetupEE" ), xorstr_( "Setup for Disaster" ), { xorstr_( "Always" ), xorstr_( "If no Dash Ready" ), xorstr_( "Disabled" ) }, 1 );
		Torment::AllowEER = MenuEE->AddCheckBox( xorstr_( "AllowEER" ), xorstr_( "Beta: Allow EE-R" ), true );

		const auto MenuR = MainMenu->AddMenu( xorstr_( "MenuR" ), xorstr_( "[R / Spiraling Despair] Settings" ) );
		MenuR->AddTexture( SpellTextures[ HweiSpell::R ] );
		SpiralingDespair::ComboR = MenuR->AddCheckBox( xorstr_( "ComboR" ), xorstr_( "Use in Combo" ), true );
		SpiralingDespair::HitchanceR = MenuR->AddList( xorstr_( "hitchanceR" ), xorstr_( "Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 3 );
		SpiralingDespair::SemiR = MenuR->AddKeyBind( xorstr_( "SemiR" ), xorstr_( "Semi R" ), 'T', false, false );
		SpiralingDespair::HitchanceSemiR = MenuR->AddList( xorstr_( "SemihitchanceR" ), xorstr_( "Semi Hitchance" ), { xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High (Slow)" ), xorstr_( "Very High (Very Slow)" ), xorstr_( "Only Dashes" ) }, 2 );
		MenuSDK::PermaShow::AddComponent( xorstr_( "Semi R" ), SpiralingDespair::SemiR );
		const auto WhitelistR = MenuR->AddMenu( xorstr_( "WhitelistR" ), xorstr_( "Whitelist" ) );

		for ( const auto& Client : ManagerTemplates::Heroes() )
		{
			if ( ShouldSkipWhitelist( Client ) )
			{
				continue;
			}

			SpiralingDespair::Whitelist[ Client->NetworkID() ] = WhitelistR->AddCheckBox( Client->SkinName(), GetDisplayName( Client ), true );
			SpiralingDespair::Whitelist[ Client->NetworkID() ]->AddTexture( Client->GetIconSquare() );
		}

		reinterpret_cast<char(__fastcall*)( HweiComponent*, std::uint32_t )>( DEFINE_RVA( Offsets::AIHeroClient::SetHweiSubject ) )( ObjectManager::GetPlayer()->GetHweiComponent(), Spell::SpellSlot_Q );
		reinterpret_cast<char(__fastcall*)( HweiComponent*, std::uint32_t )>( DEFINE_RVA( Offsets::AIHeroClient::SetHweiSubject ) )( ObjectManager::GetPlayer()->GetHweiComponent(), Spell::SpellSlot_R );

		EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );
		EventHandler::Add( Event::HudDraw, DrawHweiHud );
		EventHandler::Add( Event::OnCreateObject, OnCreate );
		EventHandler::Add( Event::OnDeleteObject, OnDelete );
		EventHandler::Add( Event::OnNewPath, Torment::OnNewPath );
		EventHandler::Add( Event::OnProcessSpell, OnProcessSpell );
		EventHandler::Add( Event::OnOrbwalkerAction, OnOrbwalkerAction );
		EventHandler::Add( Event::WorldDraw, OnDrawWorld );
		EventHandler::Add( Event::Present, OnDraw );

		HweiActions.Add( [SpecialSpells]() { Torment::InitializeEW( SpecialSpells ); }, 2 );
	}
}
