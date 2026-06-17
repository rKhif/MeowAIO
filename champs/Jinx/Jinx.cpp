#include "Jinx.h"

#include "../ChampionShared.h"
#include "../DamageLib.h"
#include "../EventHandler.h"
#include "../OrbwalkerSDK.h"

namespace Features::Champions
{
	namespace Jinx
	{
		bool MovementTracking::IsWalkingTowardsPlayer( AIBaseClient* Unit, float Time )
		{
			if ( !Unit || !Unit->IsActive() )
				return false;

			if ( !ChasingMap.contains( Unit->NetworkID() ) )
				return false;

			auto& Data = ChasingMap[ Unit->NetworkID() ];
			if ( Data.Direction != Towards )
				return false;

			return GlobalClocks::LastUpdateNetworkTime() - Data.StartTime > Time && GlobalClocks::LastUpdateNetworkTime() - Data.LastUpdateTime < 1.5f;
		}

		bool MovementTracking::IsWalkingAwayFromPlayer( AIBaseClient* Unit, float Time )
		{
			if ( !Unit || !Unit->IsActive() )
				return false;

			if ( !ChasingMap.contains( Unit->NetworkID() ) )
				return false;

			auto& Data = ChasingMap[ Unit->NetworkID() ];
			if ( Data.Direction != Away )
				return false;

			return GlobalClocks::LastUpdateNetworkTime() - Data.StartTime > Time && GlobalClocks::LastUpdateNetworkTime() - Data.LastUpdateTime < 1.5f;
		}

		bool MovementTracking::IsPlayerWalkingTowards( AIBaseClient* Unit, float Time )
		{
			if ( !Unit || !Unit->IsActive() )
				return false;

			if ( !PlayerMovementMap.contains( Unit->NetworkID() ) )
				return false;

			auto& Data = PlayerMovementMap[ Unit->NetworkID() ];
			if ( Data.Direction != Towards )
				return false;

			return GlobalClocks::LastUpdateNetworkTime() - Data.StartTime > Time && GlobalClocks::LastUpdateNetworkTime() - Data.LastUpdateTime < 1.5f;
		}

		bool MovementTracking::IsPlayerWalkingAwayFrom( AIBaseClient* Unit, float Time )
		{
			if ( !Unit || !Unit->IsActive() )
				return false;

			if ( !PlayerMovementMap.contains( Unit->NetworkID() ) )
				return false;

			auto& Data = PlayerMovementMap[ Unit->NetworkID() ];
			if ( Data.Direction != Away )
				return false;

			return GlobalClocks::LastUpdateNetworkTime() - Data.StartTime > Time && GlobalClocks::LastUpdateNetworkTime() - Data.LastUpdateTime < 1.5f;
		}

		void OnGameUpdate()
		{
			if ( SuperMegaDeathRocket::SemiAuto() )
				return;

			if ( SuperMegaDeathRocket::Auto() )
				return;

			if ( FlameChompers::Auto() )
				return;

			if ( Zap::Auto() )
				return;

			switch ( Orbwalker::SDK::GetMode() )
			{
			case Orbwalker::Mode::Combo:
				{
					if ( FlameChompers::Combo->Value && FlameChompers::Execute() )
						return;

					if ( Zap::Combo->Value && Zap::Execute() )
						return;
				}
				break;
			case Orbwalker::Mode::Laneclear: break;
			case Orbwalker::Mode::Lasthit: break;
			case Orbwalker::Mode::Harass:
				{
					if ( Zap::Harass->Value && Zap::Execute() )
						return;
				}
				break;
			case Orbwalker::Mode::Flee: break;
			case Orbwalker::Mode::None: break;
			default: ;
			}
		}

		void OnDrawWorld()
		{
			Switcheroo::Draw();
			Zap::Draw();
			FlameChompers::Draw();
			SuperMegaDeathRocket::Draw();
		}

		void OnOrbOutOfRange( Riot::AttackableUnit** Target )
		{
			Switcheroo::OnOutOfRange( Target );
		}

		void OnOrbwalkerAction( Features::Orbwalker::ActionArgs* Args, bool* Process )
		{
			if ( Args->Type != Orbwalker::ActionArgs::Before || !Args->Target )
				return;

			if ( const auto Client = Args->Target->AsAIBase() )
			{
				Switcheroo::OnBeforeAttack( Client );
			}
		}

		void OnNewPath( AIBaseClient* Sender, std::vector<Vector3f>* Nodes, bool IsDash, float DashSpeed )
		{
			if ( !Sender->IsHero() )
				return;

			if ( Sender->IsEnemy() )
			{
				if ( !ChasingMap.contains( Sender->NetworkID() ) )
				{
					ChasingMap.emplace( Sender->NetworkID(), ChasingData{ Other, 0.f, GlobalClocks::LastUpdateNetworkTime() } );
				}

				const auto PlayerPosition = ObjectManager::GetPlayer()->Position();

				if ( Nodes && !Nodes->empty() )
				{
					ChasingMap[ Sender->NetworkID() ].LastUpdateTime = GlobalClocks::LastUpdateNetworkTime();

					const auto StartPoint = Nodes->front();

					const auto ToPathEnd = StartPoint - Nodes->back();
					const auto ToPlayer = StartPoint - PlayerPosition;

					const auto Angle = ToPathEnd.Angle( ToPlayer );

					Direction UnitDir = Other;
					if ( Angle < 40.f )
					{
						UnitDir = Towards;
					}
					else if ( Angle > 140.f )
					{
						UnitDir = Away;
					}

					if ( UnitDir != ChasingMap[ Sender->NetworkID() ].Direction )
					{
						ChasingMap[ Sender->NetworkID() ].Direction = UnitDir;
						ChasingMap[ Sender->NetworkID() ].StartTime = GlobalClocks::LastUpdateNetworkTime();
					}
				}
			}

			if ( Sender->IsMe() )
			{
				for ( const auto& Hero : ManagerTemplates::Heroes() )
				{
					if ( !Hero->IsEnemy() )
						continue;

					if ( !PlayerMovementMap.contains( Hero->NetworkID() ) )
					{
						PlayerMovementMap.emplace( Hero->NetworkID(), ChasingData{ Other, 0.f, GlobalClocks::LastUpdateNetworkTime() } );
					}

					const auto HeroPosition = Hero->Position();

					if ( Nodes && !Nodes->empty() )
					{
						PlayerMovementMap[ Hero->NetworkID() ].LastUpdateTime = GlobalClocks::LastUpdateNetworkTime();

						const auto StartPoint = Nodes->front();

						const auto ToPathEnd = StartPoint - Nodes->back();
						const auto ToHero = StartPoint - HeroPosition;

						const auto Angle = ToPathEnd.Angle( ToHero );

						Direction UnitDir = Other;
						if ( Angle < 40.f )
						{
							UnitDir = Towards;
						}
						else if ( Angle > 140.f )
						{
							UnitDir = Away;
						}

						if ( UnitDir != PlayerMovementMap[ Hero->NetworkID() ].Direction )
						{
							PlayerMovementMap[ Hero->NetworkID() ].Direction = UnitDir;
							PlayerMovementMap[ Hero->NetworkID() ].StartTime = GlobalClocks::LastUpdateNetworkTime();
						}
					}
				}
			}
		}

		void OnBuffGainLose( AIBaseClient* Sender, Spell::Buff::BuffInstance* Buff, bool IsGain )
		{
			if ( Sender->IsMe() && IsGain )
			{
				if ( Buff->Hash() == HASH( "JinxQ" ) )
				{
					Switcheroo::ActiveWeapon = Switcheroo::Weapon::Fishbones;
				}

				if ( Buff->Hash() == HASH( "jinxqicon" ) )
				{
					Switcheroo::ActiveWeapon = Switcheroo::Weapon::PowPow;
				}
			}
		}

		float BiggestWidth = 0.f;

		void OnDraw()
		{
			if ( !DebugMode->Value )
				return;

			Vector2f MainCursor = { 20.f, 20.f };

			const Vector2f TextSize1 = Graphics::TextSize( Switcheroo::LastCastReason.c_str(), 16.f, false );
			Graphics::AddRectangleFilled( MainCursor, max( BiggestWidth, TextSize1.x ), ( TextSize1.y + 1.5f ) * 4.f + 10.f, Graphics::RGBA( 30, 30, 30, 80 ) );
			MainCursor.x += 5.f;
			MainCursor.y += 5.f;
			Graphics::AddText( Switcheroo::LastCastReason.c_str(), 16.f, MainCursor, Graphics::RGBA( 255, 255, 255, 255 ), false );
			MainCursor.y += 2.f + TextSize1.y;
			BiggestWidth = max( BiggestWidth, TextSize1.x + 10.f );

			const Vector2f TextSize2 = Graphics::TextSize( Zap::LastCastReason.c_str(), 16.f, false );
			Graphics::AddText( Zap::LastCastReason.c_str(), 16.f, MainCursor, Graphics::RGBA( 255, 255, 255, 255 ), false );
			MainCursor.y += 2.f + TextSize2.y;
			BiggestWidth = max( BiggestWidth, TextSize2.x + 10.f );

			const Vector2f TextSize3 = Graphics::TextSize( FlameChompers::LastCastReason.c_str(), 16.f, false );
			Graphics::AddText( FlameChompers::LastCastReason.c_str(), 16.f, MainCursor, Graphics::RGBA( 255, 255, 255, 255 ), false );
			MainCursor.y += 2.f + TextSize3.y;
			BiggestWidth = max( BiggestWidth, TextSize3.x + 10.f );

			const Vector2f TextSize4 = Graphics::TextSize( SuperMegaDeathRocket::LastCastReason.c_str(), 16.f, false );
			Graphics::AddText( SuperMegaDeathRocket::LastCastReason.c_str(), 16.f, MainCursor, Graphics::RGBA( 255, 255, 255, 255 ), false );
			MainCursor.y += 2.f + TextSize4.y;
			BiggestWidth = max( BiggestWidth, TextSize4.x + 10.f );
		}

		void Initialize()
		{
			const auto& Player = ObjectManager::GetPlayer();

			Switcheroo::Input = BaseSpell( BaseSpellData{
				.Slot = Spell::SpellSlot_Q,
			} );

			Zap::Input = BaseSpell( BaseSpellData{
				.Slot = Spell::SpellSlot_W,
				.Type = Linear,
				.CastDelay = 0.6f,
				.Range = 1450.f,
				.RangeType = Edge,
				.Speed = 3300.f,
				.Radius = 60.f,
				.HeroCollisionType = Hard,
				.MinionCollisionType = Hard,
				.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall,
				.ExtraCollisionRadius = 10.f,
			} );

			FlameChompers::Input = BaseSpell( BaseSpellData{
				.Slot = Spell::SpellSlot_E,
				.Type = Circular,
				.CastDelay = .9f,
				.Range = 925.f,
				.RangeType = Center,
				.Speed = 0.f,
				.Radius = 100.f,
				.CollisionFlags = CollideWindWall,
				.ExtraCollisionRadius = 20.f
			} );

			auto UltTimeToTarget = [ ] ( const BaseSpell&, const float Distance ) -> float
			{
				const float PreAccelDistance = min( 1350.f, Distance );
				const float PostAccelDistance = max( 0.f, Distance - PreAccelDistance );
				return .6f + PreAccelDistance / 1700.f + PostAccelDistance / 2200.f;
			};

			SuperMegaDeathRocket::Input = BaseSpell( BaseSpellData{
				.Slot = Spell::SpellSlot_R,
				.Type = Linear,
				.CastDelay = 0.6f,
				.Range = 25000.f,
				.RangeType = Edge,
				.Speed = 2200.f,
				.CalculateTimeTraveledCustom = UltTimeToTarget,
				.Radius = 140.f,
				.HeroCollisionType = Hard,
				.CollisionFlags = CollideBladeWhirl | CollideShield | CollideWindWall
			} );

			const auto MainMenu = MenuSDK::Menu::CreateMenu( xorstr_( "Jinx" ), xorstr_( "Jinx" ) );
			MainMenu->AddTexture( Player->GetIconSquare() );

			TS = TargetSelector::GetInstance();

			const auto QMenu = MainMenu->AddMenu( xorstr_( "Jinx.q" ), xorstr_( "[Q] Switcheroo!" ) );
			{
				QMenu->AddTexture( Player->GetSpell( Spell::SpellSlot_Q )->GetIcon() );
				Switcheroo::Combo = QMenu->AddCheckBox( xorstr_( "Jinx.q.combo" ), xorstr_( "Use in Combo" ), true );
				Switcheroo::AOE = QMenu->AddCheckBox( xorstr_( "Jinx.q.aoe" ), xorstr_( "Use for AOE" ), true );
				Switcheroo::FarmMinigun = QMenu->AddCheckBox( xorstr_( "Jinx.q.farmpowpow" ), xorstr_( "Force Pow Pow on non hero targets" ), true );
				Switcheroo::RangeDraw = QMenu->AddCheckBox( xorstr_( "Jinx.q.range" ), xorstr_( "Draw Range" ), true );
			}
			const auto WMenu = MainMenu->AddMenu( xorstr_( "Jinx.w" ), xorstr_( "[W] Zap!" ) );
			{
				WMenu->AddTexture( Player->GetSpell( Spell::SpellSlot_W )->GetIcon() );
				Zap::Combo = WMenu->AddCheckBox( xorstr_( "Jinx.w.combo" ), xorstr_( "Use in Combo" ), true );
				Zap::Harass = WMenu->AddCheckBox( xorstr_( "Jinx.w.harass" ), xorstr_( "Use in Harass" ), false );
				Zap::Semi = WMenu->AddKeyBind( xorstr_( "Jinx.w.key" ), xorstr_( "Semi-Auto" ), VK_F1, false, false );
				MenuSDK::PermaShow::AddComponent( xorstr_( "Semi-Auto W" ), Zap::Semi );

				WMenu->AddInfo( xorstr_( "AutoSpacer" ), xorstr_( "[Auto] Settings" ) );

				Zap::AutoCC = WMenu->AddCheckBox( xorstr_( "Jinx.w.cc" ), xorstr_( "Auto Use on CC/Stasis" ), true );
				Zap::AutoKill = WMenu->AddCheckBox( xorstr_( "Jinx.w.kill" ), xorstr_( "Auto Use on Killable" ), true );

				WMenu->AddInfo( xorstr_( "AutoSpacer" ), xorstr_( "[Advanced] Settings" ) );

				Zap::DontWInAARange = WMenu->AddCheckBox( xorstr_( "Jinx.w.aarange" ), xorstr_( "Don't use if Enemy in AA Range" ), true );
				Zap::Weave = WMenu->AddCheckBox( xorstr_( "Jinx.w.weave" ), xorstr_( "Try to Weave between AAs" ), true );
				Zap::RangeDraw = WMenu->AddCheckBox( xorstr_( "Jinx.w.range" ), xorstr_( "Draw Range" ), true );
			}
			const auto EMenu = MainMenu->AddMenu( xorstr_( "Jinx.e" ), xorstr_( "[E] Flame Chompers!" ) );
			{
				EMenu->AddTexture( Player->GetSpell( Spell::SpellSlot_E )->GetIcon() );
				FlameChompers::Combo = EMenu->AddCheckBox( xorstr_( "Jinx.e.combo" ), xorstr_( "Use in Combo" ), true );
				FlameChompers::RangeDraw = EMenu->AddCheckBox( xorstr_( "Jinx.e.range" ), xorstr_( "Draw Range" ), true );
				EMenu->AddInfo( xorstr_( "Auto" ), xorstr_( "[Auto] Settings" ) );
				FlameChompers::AutoCC = EMenu->AddCheckBox( xorstr_( "Jinx.e.cc" ), xorstr_( "On CC" ), true );
				FlameChompers::AutoStasis = EMenu->AddCheckBox( xorstr_( "Jinx.e.stasis" ), xorstr_( "On Stasis/Revive" ), true );
				FlameChompers::AutoGapClose = EMenu->AddCheckBox( xorstr_( "Jinx.e.gap" ), xorstr_( "On Gap Close" ), true );
				FlameChompers::AutoUndodgeable = EMenu->AddCheckBox( xorstr_( "Jinx.e.undodgeable" ), xorstr_( "On Undodgeable" ), true );
			}
			const auto RMenu = MainMenu->AddMenu( xorstr_( "Jinx.r" ), xorstr_( "[R] Super Mega Death Rocket!" ) );
			{
				RMenu->AddTexture( Player->GetSpell( Spell::SpellSlot_R )->GetIcon() );
				SuperMegaDeathRocket::Kill = RMenu->AddCheckBox( xorstr_( "Jinx.r.kill" ), xorstr_( "Auto Killsteal" ), true );
				SuperMegaDeathRocket::Keypress = RMenu->AddKeyBind( xorstr_( "Jinx.r.key" ), xorstr_( "Semi-Auto" ), 'G', false, false );
				SuperMegaDeathRocket::MaxRange = RMenu->AddSliderF( xorstr_( "Jinx.R.MaxRadius" ), xorstr_( "Max Cast Range" ), 1500.f, 800.f, 4000.f, 50.f );
				SuperMegaDeathRocket::RangeDraw = RMenu->AddCheckBox( xorstr_( "Jinx.r.range" ), xorstr_( "Draw Range" ), true );
				SuperMegaDeathRocket::UseCursorRadius = RMenu->AddCheckBox( xorstr_( "Jinx.R.UseCursorRadius" ), xorstr_( "Use Cursor Radius" ), true );
				SuperMegaDeathRocket::UseCursorRadius->AddTooltip( xorstr_( "Will only target enemies inside of the set radius around your cursor." ) );
				SuperMegaDeathRocket::CursorRadius = RMenu->AddSlider( xorstr_( "Jinx.R.CursorRadius" ), xorstr_( "Cursor Radius" ), 700, 200, 2500, 50 );

				MenuSDK::PermaShow::AddComponent( xorstr_( "Semi-Auto R" ), SuperMegaDeathRocket::Keypress );
			}

			DebugMode = MainMenu->AddCheckBox( xorstr_( "DebugMode" ), xorstr_( "Debug Mode" ), true );
			DebugMode->AddTooltip( xorstr_( "Please Enable this if you're planning to send feedback with clips" ) );

			EventHandler::Add( Event::OnGameUpdate, OnGameUpdate );
			EventHandler::Add( Event::WorldDraw, OnDrawWorld );
			EventHandler::Add( Event::OnOrbwalkerAction, OnOrbwalkerAction );
			EventHandler::Add( Event::OnOutOfRange, OnOrbOutOfRange );
			EventHandler::Add( Event::OnBuffGainLose, OnBuffGainLose );
			EventHandler::Add( Event::OnNewPath, OnNewPath );
			EventHandler::Add( Event::OnBuffGainLose, FlameChompers::OnBuffGainLose );
			EventHandler::Add( Event::Present, OnDraw );
		}

		void Destroy()
		{
			EventHandler::Remove( Event::OnGameUpdate, OnGameUpdate );
			EventHandler::Remove( Event::WorldDraw, OnDrawWorld );
		}
	}
}
