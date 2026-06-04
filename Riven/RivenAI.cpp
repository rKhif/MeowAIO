#include "RivenAI.h"

#include <random>

#include "ClearMode.h"
#include "OrbwalkerSDK.h"
#include "OrbwalkerData.h"
#include "RivenAAPred.h"
#include "RivenCrescent.h"
#include "RivenE.h"
#include "RivenEFlashQPred.h"
#include "RivenEFlashWPred.h"
#include "RivenEPred.h"
#include "RivenEQPred.h"
#include "RivenEWPred.h"
#include "RivenFlashQPred.h"
#include "RivenFlashWPred.h"
#include "RivenQPred.h"
#include "RivenQ.h"
#include "RivenWPred.h"
#include "RivenMenu.h"
#include "RivenR1Pred.h"
#include "RivenR2.h"
#include "RivenR2DmgPred.h"
#include "RivenR2Pred.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Core::AI
		{
			std::function<bool()> OnEndFunction = nullptr;
			float OnEndTime = 0.f;
			std::vector<std::function<void()>> OnCastQX = {};

			float GetRandomValue( float max = 10.f )
			{
				std::random_device rd;
				std::mt19937 e2( rd() );
				std::uniform_real_distribution<> dist( 0.f, max );
				return dist( e2 );
			}

			bool PreR2 = false;
			bool PreFlash = false;
			float LastE = 0.f;
			float LastSpell = 0.f;

			bool PushEnabled()
			{
				return Orbwalker::SDK::GetMode() == Orbwalker::Mode::Laneclear && Orbwalker::Config::ModifierKey->Value;
			}

			void MoveToMouse( float distance )
			{
				auto p1 = Riot::HudManager::GetInstance()->CursorTargetLogic()->CursorPosition();
				auto p2 = Riot::ObjectManager::GetPlayer()->Position();
				if ( p1.Distance2D( p2 ) < 5.f )
				{
					if ( const auto OrbTarget = Orbwalker::SDK::GetComboTarget() )
					{
						p1 = OrbTarget->Position();
					}
					if ( PushEnabled() )
					{
						auto res = Pred::AA::GetPushResult();
						if ( res )
						{
							p1 = res->Position();
						}
					}
					if ( p1.Distance2D( p2 ) < 5.f )
					{
						float RandomOffset = GetRandomValue();
						p1 = Riot::Vector3f{ p1.x + RandomOffset, p1.y, p1.x + RandomOffset };
					}
				}
				ObjectManager::GetPlayer()->Move( p2.Lerp( p1, distance / p1.Distance( p2 ) ), true, true, true );
			}

			bool OnEndQ()
			{
				OnEndFunction = nullptr;
				Orbwalker::SDK::Reset();
				Orbwalker::SDK::SetPause( 0.f );
				if ( Pred::R2Dmg::GetActionState() )
				{
					Pred::R2Dmg::InvokeAction( true );
					return true;
				}
				if ( !Orbwalker::SDK::GetComboTarget() )
				{
					if ( Pred::W::GetActionState( ObjectManager::GetPlayer()->Position() ) )
					{
						Pred::W::InvokeAction( true );
					}
				}

				return false;
			}

			bool OnEndW()
			{
				OnEndFunction = nullptr;
				Orbwalker::SDK::SetPause( 0.f );

				if ( PushEnabled() )
				{
					if ( Menu::PushW->Value )
					{
						auto W = Pred::W::GetPushState();
						if ( W )
						{
							Pred::W::InvokeAction( true );
							if ( Pred::Q::GetSpellState() )
							{
								ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, W );
							}
							return true;
						}
					}
					if ( Pred::Q::GetPushState() )
					{
						Pred::Q::InvokeAction( true );
						return true;
					}
				}

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				HudManager::GetInstance()->InputLogic()->PlayerStopPositionClick();

				if ( Pred::R2Dmg::GetActionState() )
				{
					Pred::R2Dmg::InvokeAction( true );
					return true;
				}

				auto Q = Pred::Q::GetActionState( Riot::ObjectManager::GetPlayer()->Position() );
				auto E = Pred::E::GetActionState();
				auto EQ = Pred::EQ::GetActionState();
				auto R1 = Pred::R1::GetActionState();
				auto RH = Item::Crescent::GetActionState();

				if ( EQ && Q )
				{
					if ( std::get<0>( EQ.value() )->Handle() != Q->Handle() )
					{
						Pred::EQ::InvokeAction( true );
						if ( Menu::R1->Value && Pred::R1::GetActionState() )
						{
							Pred::R1::InvokeAction( true );
						}
						return true;
					}
				}

				if ( E && Q )
				{
					if ( E->Handle() != Q->Handle() )
					{
						Pred::E::InvokeAction( true );
						if ( Menu::R1->Value && R1 )
						{
							Pred::R1::InvokeAction( true );
						}
						return true;
					}
				}

				if ( !Orbwalker::SDK::CanAttack() )
				{
					if ( Q )
					{
						Pred::Q::InvokeAction( true );
						return true; //W->Q
					}
					if ( RH )
					{
						Item::Crescent::InvokeAction( true );
						return true;
					}
				}

				return false;
			}

			bool OnEndE()
			{
				OnEndFunction = nullptr;
				Orbwalker::SDK::SetPause( 0.f );
				//TODO Push

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				if ( Pred::R2Dmg::GetActionState() )
				{
					Pred::R2Dmg::InvokeAction( true );
					return true;
				}

				auto W = Pred::W::GetActionState( Spell::E::Position );
				auto Q = Pred::Q::GetActionState( Spell::E::Position );
				auto FlashW = Pred::FlashW::GetActionState( Spell::E::Position );
				auto FlashQ = Pred::FlashQ::GetActionState( Spell::E::Position );

				if ( TargetSelector::GetSelectedTarget() && Menu::Flash->Value )
				{
					if ( FlashW )
					{
						if ( !W && !Q )
						{
							Pred::FlashW::InvokeAction( true, false );
							if ( Pred::Q::GetSpellState() )
							{
								ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
							}
							return true;
						}
					}
					if ( FlashQ )
					{
						if ( !Q )
						{
							if ( !Pred::W::GetSpellState() )
							{
								Pred::FlashQ::InvokeAction( true, false );
								return true;
							}
						}
					}
				}

				if ( Menu::EAA->Value )
				{
					if ( Orbwalker::SDK::GetComboTarget() && Orbwalker::SDK::CanAttack() )
					{
						return true;
					}
				}

				if ( W )
				{
					if ( Q )
					{
						if ( W->Handle() != Q->Handle() )
						{
							Pred::Q::InvokeAction( true );
							return true;
						}

						Pred::W::InvokeAction( true );
						Pred::Q::InvokeAction( true );
						return true;
					}
					Pred::W::InvokeAction( true );
					return true;
				}

				if ( Q )
				{
					Pred::Q::InvokeAction( true );
					return true;
				}

				return false;
			}

			bool OnEndR1()
			{
				OnEndFunction = nullptr;
				Orbwalker::SDK::SetPause( 0.f );

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				HudManager::GetInstance()->InputLogic()->PlayerStopPositionClick();

				Riot::Vector3f Pos = ObjectManager::GetPlayer()->Position();

				if ( GlobalClocks::LastUpdateNetworkTime() - LastE < 0.3f )
				{
					Pos = Spell::E::Position;
				}

				auto W = Pred::W::GetActionState( Pos );
				auto Q = Pred::Q::GetActionState( Pos );
				auto FlashW = Pred::FlashW::GetActionState( Pos );
				auto FlashQ = Pred::FlashQ::GetActionState( Pos );

				if ( TargetSelector::GetSelectedTarget() && Menu::Flash->Value )
				{
					if ( FlashW )
					{
						if ( !W && !Q )
						{
							Pred::FlashW::InvokeAction( true, false );
							if ( CanDoubleCast() && Pred::Q::GetSpellState() )
							{
								ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
							}
							return true;
						}
					}
					if ( FlashQ )
					{
						if ( !Q )
						{
							if ( !Pred::W::GetSpellState() )
							{
								Pred::FlashQ::InvokeAction( true, false );
								return true;
							}
						}
					}
				}

				if ( Menu::EAA->Value )
				{
					if ( Orbwalker::SDK::GetComboTarget() && Orbwalker::SDK::CanAttack() )
					{
						return true;
					}
				}

				if ( W )
				{
					if ( Q )
					{
						if ( W->Handle() != Q->Handle() )
						{
							Pred::Q::InvokeAction( true );
							return true;
						}
						Pred::W::InvokeAction( true );
						Pred::Q::InvokeAction( true );
						return true;
					}
					Pred::W::InvokeAction( true );
					return true;
				}

				if ( Q )
				{
					Pred::Q::InvokeAction( true );
					return true;
				}

				return false;
			}

			bool OnEndR2()
			{
				OnEndFunction = nullptr;
				Orbwalker::SDK::SetPause( 0.f );

				auto PlayerPos = ObjectManager::GetPlayer()->Position();
				auto Q = Pred::Q::GetActionState( PlayerPos );
				auto W = Pred::W::GetActionState( PlayerPos );
				auto EW = Pred::EW::GetActionState();
				auto FlashW = Pred::FlashW::GetActionState( PlayerPos );
				auto FlashQ = Pred::FlashQ::GetActionState( PlayerPos );

				if ( !PreR2 )
				{
					if ( TargetSelector::GetSelectedTarget() && Menu::Flash->Value )
					{
						if ( FlashW )
						{
							if ( !W && !Q && !EW )
							{
								if ( CanDoubleCast() && Pred::Q::GetSpellState() )
								{
									Pred::FlashW::InvokeAction( true, false );
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
									return true;
								}
							}
						}
						if ( FlashQ )
						{
							if ( !Q )
							{
								if ( !Pred::W::GetSpellState() )
								{
									Pred::FlashQ::InvokeAction( true, false );
									return true;
								}
							}
						}
					}
				}

				if ( PreFlash )
				{
					if ( FlashW )
					{
						if ( !W && !Q && !EW )
						{
							if ( Pred::Q::GetSpellState() )
							{
								if ( !Pred::E::GetSpellState() )
								{
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_W, FlashW->Position );
									Pred::FlashW::InvokeAction( true, false );
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
									return true;
								}
								if ( CanDoubleCast() )
								{
									Pred::FlashW::InvokeAction( true, false );
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
									return true;
								}
							}
							Pred::FlashW::InvokeAction( true, false );
							return true;
						}
					}
					if ( FlashQ )
					{
						if ( !W && !Q && !EW )
						{
							Pred::FlashQ::InvokeAction( true, false );
							return true;
						}
					}
				}

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				HudManager::GetInstance()->InputLogic()->PlayerStopPositionClick();

				if ( W )
				{
					if ( Q )
					{
						if ( W->Handle() != Q->Handle() )
						{
							Pred::Q::InvokeAction( true );
							return true;
						}
						if ( CanDoubleCast() )
						{
							Pred::W::InvokeAction( true );
							Pred::Q::InvokeAction( true );
							return true;
						}
					}
					Pred::W::InvokeAction( true );
					return true;
				}

				if ( Q )
				{
					Pred::Q::InvokeAction( true );
					return true;
				}

				return false;
			}

			bool OnEndPreR2()
			{
				OnEndFunction = OnEndR2;
				OnEndTime = Spell::R2::LastCastTime + 0.267f;

				if ( !PreR2 )
					return false;

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				Riot::Vector3f Pos = ObjectManager::GetPlayer()->Position();

				if ( GlobalClocks::LastUpdateNetworkTime() - LastE < 0.3f )
				{
					Pos = Spell::E::Position;
				}

				auto W = Pred::W::GetActionState( Pos );
				auto Q = Pred::Q::GetActionState( Pos );
				auto FlashW = Pred::FlashW::GetActionState( Pos );
				auto FlashQ = Pred::FlashQ::GetActionState( Pos );

				PreFlash = false;

				if ( TargetSelector::GetSelectedTarget() && Menu::Flash->Value )
				{
					if ( FlashW )
					{
						if ( !W && !Q )
						{
							Pred::FlashW::InvokeAction( true, true );
							PreFlash = true;
							return true;
						}
					}

					if ( FlashQ )
					{
						if ( !Q )
						{
							if ( !Pred::W::GetSpellState() )
							{
								Pred::FlashQ::InvokeAction( true, true );
								PreFlash = true;
								return true;
							}
						}
					}
				}

				return false;
			}

			bool OnEndCrescent()
			{
				OnEndFunction = nullptr;
				Orbwalker::SDK::SetPause( 0.f );

				//TODO push

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				HudManager::GetInstance()->InputLogic()->PlayerStopPositionClick();

				if ( Pred::R2Dmg::GetActionState() )
				{
					Pred::R2Dmg::InvokeAction( true );
					return true;
				}

				auto W = Pred::W::GetActionState( ObjectManager::GetPlayer()->Position() );
				auto Q = Pred::Q::GetActionState( ObjectManager::GetPlayer()->Position() );
				auto E = Pred::E::GetActionState();
				auto EW = Pred::EW::GetActionState();

				if ( Menu::R1->Value )
				{
					if ( Pred::R1::GetActionState() )
					{
						if ( EW )
						{
							Pred::EW::InvokeAction( true );
						}
						else if ( E )
						{
							Pred::E::InvokeAction( true );
						}
						Pred::R1::InvokeAction( false );
					}
				}

				if ( W )
				{
					if ( Q )
					{
						if ( W->Handle() != Q->Handle() )
						{
							Pred::Q::InvokeAction( true );
							return true;
						}
						Pred::W::InvokeAction( true );
						Pred::Q::InvokeAction( true );
						return true;
					}
					Pred::W::InvokeAction( true );
					return true;
				}

				if ( !Orbwalker::SDK::CanAttack( 0.2f ) )
				{
					if ( Q )
					{
						Pred::Q::InvokeAction( true );
						return true;
					}
				}

				return false;
			}

			bool OnMoveQ()
			{
				MoveToMouse( 400.f );
				OnEndFunction = OnEndQ;
				OnEndTime += 0.150f + 0.125f;
				return true;
			}

			bool OnMoveQ3()
			{
				MoveToMouse( 400.f );
				OnEndFunction = OnEndQ;
				OnEndTime += 0.250f + 0.125f;
				return true;
			}

			void OnCastQ()
			{
				OnEndFunction = OnMoveQ;
				OnEndTime = Riot::GlobalClocks::LastUpdateNetworkTime() + 0.15f;
			}

			void OnCastQ3()
			{
				OnEndFunction = OnMoveQ3;
				OnEndTime = Riot::GlobalClocks::LastUpdateNetworkTime() + 0.25f;
			}

			void OnCastQMain()
			{
				LastE = 0.f;
				Orbwalker::SDK::SetPause( 2.f );
				OnCastQX[ Spell::Q::GetSpell()->Ammo() ]();
			}

			void OnCastW()
			{
				if ( GlobalClocks::LastUpdateNetworkTime() + 0.267f > OnEndTime )
				{
					OnEndFunction = OnEndW;
					OnEndTime = GlobalClocks::LastUpdateNetworkTime() + 0.267f;
					Orbwalker::SDK::SetPause( 2.f );
				}
			}

			void OnCastE()
			{
				LastE = GlobalClocks::LastUpdateNetworkTime();
				if ( GlobalClocks::LastUpdateNetworkTime() + 0.237f > OnEndTime )
				{
					OnEndFunction = OnEndE;
					OnEndTime = GlobalClocks::LastUpdateNetworkTime() + 0.237f;
					if ( !Menu::EAA->GetValue() )
					{
						Orbwalker::SDK::SetPause( 0.f );
					}
				}
			}

			void OnCastR1()
			{
				if ( GlobalClocks::LastUpdateNetworkTime() + 0.250f > OnEndTime )
				{
					OnEndFunction = OnEndR1;
					OnEndTime = GlobalClocks::LastUpdateNetworkTime() + 0.250f;
					if ( GlobalClocks::LastUpdateNetworkTime() - LastE < 0.033f )
					{
						OnEndTime = GlobalClocks::LastUpdateNetworkTime() + 0.267f;
					}
					Orbwalker::SDK::SetPause( 2.f );
				}
			}

			void OnCastR2()
			{
				if ( GlobalClocks::LastUpdateNetworkTime() + 0.250f > OnEndTime )
				{
					OnEndFunction = OnEndPreR2;
					OnEndTime = GlobalClocks::LastUpdateNetworkTime() + 0.250f;
					Orbwalker::SDK::SetPause( 2.f );
				}
			}

			void OnCastCrescent( Riot::Spell::SpellCastInfo* CastInfo )
			{
				if ( Riot::GlobalClocks::LastUpdateNetworkTime() + CastInfo->CastDelay() > OnEndTime )
				{
					OnEndFunction = OnEndCrescent;
					OnEndTime = Riot::GlobalClocks::LastUpdateNetworkTime() + CastInfo->CastDelay();
					Orbwalker::SDK::SetPause( 2.f );
				}
			}

			bool CanDoubleCast()
			{
				return GlobalClocks::LastUpdateNetworkTime() + MultiplayerClient::GetInstance()->CurrentRoundTripTime() < LastE + DOUBLE_CAST_TIMEOUT;
			}

			bool OnAfterAttack( AttackableUnit* Target )
			{
				if ( PushEnabled() )
				{
					if ( Menu::PushE->Value && Menu::PushW->Value )
					{
						if ( Pred::EW::GetPushState() )
						{
							if ( Pred::EW::GetSpellState() )
							{
								Pred::EW::InvokeAction( true );
								if ( Item::Crescent::GetSpellState() )
								{
									Item::Crescent::InvokeAction( true );
								}
								return true;
							}
						}
					}
					if ( Item::Crescent::GetSpellState() )
					{
						Item::Crescent::InvokeAction( true );
						return true;
					}
					auto W = Pred::W::GetPushState();
					if ( Menu::PushW->Value && W )
					{
						if ( Pred::Q::GetSpellState() && CanDoubleCast() )
						{
							Pred::W::InvokeAction( true );
							ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, W );
							return true;
						}
					}
					if ( Pred::Q::GetPushState() )
					{
						Pred::Q::InvokeAction( true );
						return true;
					}
					if ( Menu::PushW->Value && W )
					{
						Pred::W::InvokeAction( true );
						return true;
					}
					if ( Menu::PushE->Value )
					{
						if ( Pred::E::GetPushState() )
						{
							Pred::E::InvokeAction( true );
							return true;
						}
					}
				}

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return false;

				auto W = Pred::W::GetActionState( ObjectManager::GetPlayer()->Position() );
				auto Q = Pred::Q::GetActionState( ObjectManager::GetPlayer()->Position() );
				auto E = Pred::E::GetActionState();
				auto EW = Pred::EW::GetActionState();
				auto RH = Item::Crescent::GetActionState();

				if ( Menu::R1->Value && Target && Target->IsHero() )
				{
					if ( Pred::R1::GetActionState() )
					{
						if ( EW )
						{
							Pred::EW::InvokeAction( true );
						}
						else if ( E )
						{
							Pred::E::InvokeAction( true );
						}
						Pred::R1::InvokeAction( false );
						return true;
					}
				}

				if ( Pred::R2Dmg::GetActionState() )
				{
					Pred::R2Dmg::InvokeAction( true );
					return true;
				}

				if ( EW && GetRandomValue( 100.f ) <= Menu::EDoubleCastWeight->Value )
				{
					Pred::EW::InvokeAction( true );
					if ( Item::Crescent::GetSpellState() )
					{
						Item::Crescent::InvokeAction( true );
					}
					return true;
				}

				if ( W )
				{
					if ( EW )
					{
						if ( std::get<0>( EW.value() )->Handle() == W->Handle() )
						{
							Pred::EW::InvokeAction( true );
							if ( Item::Crescent::GetSpellState() )
							{
								Item::Crescent::InvokeAction( false );
							}
							return true;
						}
					}
					if ( E )
					{
						if ( E->Handle() != W->Handle() )
						{
							Pred::E::InvokeAction( true );
							if ( Item::Crescent::GetSpellState() )
							{
								Item::Crescent::InvokeAction( false );
							}
							return true; // AA -> E -> W
						}
					}

					if ( RH )
					{
						Item::Crescent::InvokeAction( true );
						return true;
					}

					if ( Q )
					{
						if ( W->Handle() != Q->Handle() )
						{
							Pred::Q::InvokeAction( true );
							return true; // AA -> Q
						}
						if ( CanDoubleCast() )
						{
							Pred::W::InvokeAction( true );
							Pred::Q::InvokeAction( true );
							return true; // AA -> W+Q
						}
					}

					Pred::W::InvokeAction( true );
					return true; // AA -> W
				}

				if ( EW )
				{
					Pred::EW::InvokeAction( true );
					if ( Item::Crescent::GetSpellState() )
					{
						Item::Crescent::InvokeAction( false );
					}
					return true;
				}

				if ( E )
				{
					Pred::E::InvokeAction( true );
					if ( Item::Crescent::GetSpellState() )
					{
						Item::Crescent::InvokeAction( false );
					}
					return true; // AA -> E -> AA
				}

				if ( Q )
				{
					Pred::Q::InvokeAction( true );
					return true;
				}

				return false;
			}

			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo )
			{
				switch ( CastInfo->GetNameHash() )
				{
				case HASH( "RivenTriCleave" ):
					{
						OnCastQMain();
						LastSpell = GlobalClocks::LastUpdateNetworkTime();
					}
					break;
				case HASH( "RivenMartyr" ):
					{
						OnCastW();
						LastSpell = GlobalClocks::LastUpdateNetworkTime();
					}
					break;
				case HASH( "RivenFeint" ):
					{
						OnCastE();
						LastSpell = GlobalClocks::LastUpdateNetworkTime();
					}
					break;
				case HASH( "RivenFengShuiEngine" ):
					{
						OnCastR1();
						LastSpell = GlobalClocks::LastUpdateNetworkTime();
					}
					break;
				case HASH( "RivenIzunaBlade" ):
					{
						OnCastR2();
						LastSpell = GlobalClocks::LastUpdateNetworkTime();
					}
					break;
				case HASH( "6029Active" ):
				case HASH( "6630Active" ):
				case HASH( "6631Active" ):
				case HASH( "7015Active" ):
				case HASH( "7016Active" ):
					{
						OnCastCrescent( CastInfo );
						LastSpell = GlobalClocks::LastUpdateNetworkTime();
					}
					break;
				}
			}

			void OnCreateObject( Riot::GameObject* Object )
			{
				auto ObjectName = Object->Name();
				if ( Object->Name().contains( "Riven_" ) && Object->Name().contains( "_detonate" ) && Object->Name().contains( "_Q_" ) )
				{
					OnEndFunction = OnEndQ;
					OnEndTime = Riot::GlobalClocks::LastUpdateNetworkTime() + 0.125f;
				}
			}

			void General()
			{
				if ( PushEnabled() )
				{
					if ( Orbwalker::SDK::CanAttack() )
					{
						//TODO this stinks
						auto [ Target, Intent ] = Orbwalker::ClearMode::GetTarget();

						if ( Target )
						{
							return;
						}
					}
					if ( Menu::PushE->Value && Menu::PushW->Value )
					{
						if ( Pred::EW::GetPushState() )
						{
							if ( Pred::EW::GetSpellState() )
							{
								Pred::EW::InvokeAction( true );
								if ( Item::Crescent::GetSpellState() )
								{
									Item::Crescent::InvokeAction( true );
								}
								return;
							}
						}
					}
					if ( Menu::PushE->Value )
					{
						if ( !Menu::PushW->Value || !Pred::W::GetSpellState() )
						{
							if ( Pred::EQ::GetPushState() )
							{
								Pred::EQ::InvokeAction( true );
								return;
							}
						}
					}
					if ( Pred::Q::GetPushState() )
					{
						Pred::Q::InvokeAction( true );
						return;
					}
				}

				if ( Orbwalker::SDK::GetMode() != Orbwalker::Mode::Combo )
					return;

				auto Q = Pred::Q::GetActionState( ObjectManager::GetPlayer()->Position() );
				auto W = Pred::W::GetActionState( ObjectManager::GetPlayer()->Position() );
				auto E = Pred::E::GetActionState();
				auto EW = Pred::EW::GetActionState();
				auto EQ = Pred::EQ::GetActionState();

				if ( TargetSelector::GetSelectedTarget() && Menu::Flash->Value )
				{
					auto FlashQ = Pred::FlashQ::GetActionState( ObjectManager::GetPlayer()->Position() );
					auto FlashW = Pred::FlashW::GetActionState( ObjectManager::GetPlayer()->Position() );
					auto EFlashW = Pred::EFlashW::GetActionState();
					auto EFlashQ = Pred::EFlashQ::GetActionState();

					if ( FlashW && !Menu::FlashOnlyR->Value )
					{
						if ( !W && !Q && !EW )
						{
							if ( Pred::Q::GetSpellState() )
							{
								if ( Pred::E::GetSpellState() )
								{
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_E, FlashW->Position );
									Pred::FlashW::InvokeAction( true, false );
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
									return;
								}
								if ( CanDoubleCast() )
								{
									Pred::FlashW::InvokeAction( true, false );
									ObjectManager::GetPlayer()->CastSpell( Riot::Spell::SpellSlot_Q, FlashW->Object );
									return;
								}
							}
							Pred::FlashW::InvokeAction( true, false );
							return;
						}
					}

					if ( FlashQ && !Menu::FlashOnlyR->Value )
					{
						if ( !Q && !EQ )
						{
							if ( Pred::W::GetSpellState() )
							{
								Pred::FlashQ::InvokeAction( true, false );
								return;
							}
						}
					}

					if ( EFlashW )
					{
						if ( !EW )
						{
							if ( Pred::R1::GetActionState() )
							{
								Pred::EFlashW::InvokeAction( true );
								Pred::R1::InvokeAction( true );
								return;
							}

							PreR2 = true;

							auto Pos = EFlashW->Position;
							if ( GetRandomValue( 100.f ) <= Menu::R2Flash->Value )
							{
								PreR2 = false;
								auto Distance = ObjectManager::GetPlayer()->Position().Distance( Pos );
								Pos = ObjectManager::GetPlayer()->Position().Lerp( Pos, Distance / 225.f );
							}
							if ( Pred::R2::GetActionState( Pos ) )
							{
								Pred::EFlashW::InvokeAction( true );
								Pred::R2::InvokeAction( true );
								return;
							}
							if ( !Menu::FlashOnlyR->Value )
							{
								Pred::EFlashW::InvokeAction( true );
								return;
							}
						}
					}

					if ( EFlashQ )
					{
						if ( !EQ )
						{
							if ( !Pred::W::GetSpellState() )
							{
								if ( Pred::R1::GetActionState() )
								{
									Pred::EFlashQ::InvokeAction( true );
									Pred::R1::InvokeAction( true );
									return;
								}
								PreR2 = true;
								auto Pos = EFlashQ->Position;
								if ( GetRandomValue( 100.f ) <= Menu::R2Flash->Value )
								{
									PreR2 = false;
									auto Distance = ObjectManager::GetPlayer()->Position().Distance( Pos );
									Pos = ObjectManager::GetPlayer()->Position().Lerp( Pos, Distance / 225.f );
								}
								if ( Pred::R2::GetActionState( Pos ) )
								{
									Pred::EFlashQ::InvokeAction( true );
									Pred::R2::InvokeAction( true );
									return;
								}
								if ( !Menu::FlashOnlyR->Value )
								{
									Pred::EFlashQ::InvokeAction( true );
									return;
								}
							}
						}
					}
				}

				if ( Orbwalker::SDK::GetComboTarget() || GlobalClocks::LastUpdateNetworkTime() + MultiplayerClient::GetInstance()->CurrentRoundTripTime() <= OnEndTime )
					return;

				if ( Pred::R2Dmg::GetActionState() )
				{
					Pred::R2Dmg::InvokeAction( true );
					return;
				}

				const auto ooc = GlobalClocks::LastUpdateNetworkTime() - LastSpell;

				if ( ( Menu::GapEW->Value || ooc < 0.75f ) && EW )
				{
					Pred::EW::InvokeAction( true );
					if ( Menu::R1->Value && Pred::R1::GetActionState() )
					{
						Pred::R1::InvokeAction( true );
					}
					else if ( Item::Crescent::GetSpellState() )
					{
						Item::Crescent::InvokeAction( true );
					}
					return;
				}

				if ( ( Menu::GapEQ->Value || ooc < 0.75f ) && EQ )
				{
					if ( !Pred::W::GetSpellState() )
					{
						Pred::EQ::InvokeAction( true );
						if ( Menu::R1->Value && Pred::R1::GetActionState() )
						{
							Pred::R1::InvokeAction( true );
						}
						else if ( Item::Crescent::GetSpellState() )
						{
							Item::Crescent::InvokeAction( true );
						}
						return;
					}
				}

				if ( ( Menu::GapQ->Value || ooc < 0.75f ) && Q )
				{
					if ( ( Menu::GapEAA->Value || ooc < 0.75f ) && E )
					{
						if ( Menu::R1->Value && Pred::R1::GetActionState() )
						{
							Pred::E::InvokeAction( true );
							Pred::R1::InvokeAction( true );
							return;
						}
						else if ( Item::Crescent::GetSpellState() )
						{
							Pred::E::InvokeAction( true );
							Item::Crescent::InvokeAction( true );
						}
					}
					Pred::Q::InvokeAction( true );
					return;
				}

				if ( ( Menu::GapEAA->Value || ooc < 0.75f ) && E )
				{
					Pred::E::InvokeAction( true );
					if ( Menu::R1->Value && Pred::R1::GetActionState() )
					{
						Pred::R1::InvokeAction( true );
					}
					else if ( Item::Crescent::GetSpellState() )
					{
						Pred::E::InvokeAction( true );
						Item::Crescent::InvokeAction( true );
					}
					return;
				}
			}

			void GetAction()
			{
				if ( OnEndFunction )
				{
					if ( Riot::GlobalClocks::LastUpdateNetworkTime() + Riot::MultiplayerClient::GetInstance()->CurrentRoundTripTime() > OnEndTime )
					{
						OnEndFunction();
					}
				}
				if ( Orbwalker::SDK::CanAction() && !Orbwalker::SDK::IsPaused() )
				{
					General();
				}
			}

			void Initialize()
			{
				OnCastQX = { OnCastQ, OnCastQ, OnCastQ3, OnCastQ3 };
			}
		}
	}
}
