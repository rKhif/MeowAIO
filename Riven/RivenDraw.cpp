#include "RivenDraw.h"

#include "Prediction.h"
#include "RivenAAPred.h"
#include "SDK.h"
#include "RivenMenu.h"
#include "RivenEWPred.h"
#include "RivenQPred.h"
#include "RivenR1Pred.h"
#include "RivenCrescent.h"
#include "RivenEPred.h"
#include "RivenEQPred.h"
#include "RivenAI.h"
#include "RivenFlash.h"
#include "RivenEFlashQPred.h"
#include "RivenEFlashWPred.h"
#include "RivenFlashQPred.h"
#include "RivenFlashWPred.h"
#include "RivenR2Pred.h"
#include "RivenWPred.h"
#include "TargetSelector.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Draw
		{
			std::optional<Combo> GetFlashCombo()
			{
				auto P2 = Riot::ObjectManager::GetPlayer()->Position();

				if ( Pred::EFlashW::GetSpellState() )
				{
					Prediction::PredictionData Data;
					Prediction::GetPositionAfterTime( TargetSelector::GetSelectedTarget(), Pred::EFlashW::GetTotalDelay(), &Data );
					auto P1 = Data.Position;
					if ( P1.Distance( P2 ) > Pred::FlashW::GetTotalRadius() )
					{
						auto R1 = Pred::FlashW::GetTotalRadius();
						auto R2 = Pred::EFlashW::GetTotalRadius();
						if ( Pred::R1::GetSpellState() )
						{
							if ( Pred::EQ::GetSpellState() )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+R1->[F+W+Q]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E+R1->[F+W]" ) };
						}
						if ( Pred::R2::GetSpellState() )
						{
							if ( Pred::EQ::GetSpellState() )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+R2->[F+W+Q]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E+R2->[F+W]" ) };
						}
						if ( !Menu::FlashOnlyR->Value )
						{
							if ( Pred::EQ::GetSpellState() )
							{
								return Combo{ P1, R1, R2, xorstr_( "E->[F+W+Q]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E->[F+W]" ) };
						}
					}
				}

				if ( Pred::EFlashQ::GetSpellState() )
				{
					if ( !Pred::W::GetSpellState() )
					{
						Prediction::PredictionData Data;
						Prediction::GetPositionAfterTime( TargetSelector::GetSelectedTarget(), Pred::EFlashQ::GetTotalDelay(), &Data );
						auto P1 = Data.Position;
						if ( P1.Distance( P2 ) > Pred::FlashQ::GetTotalRadius() )
						{
							auto R1 = Pred::FlashQ::GetTotalRadius();
							auto R2 = Pred::EFlashQ::GetTotalRadius();
							if ( Pred::R1::GetSpellState() )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+R1->[F+Q]" ) };
							}
							if ( Pred::R2::GetSpellState() )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+R2->[F+Q]" ) };
							}
							if ( !Menu::FlashOnlyR->Value )
							{
								return Combo{ P1, R1, R2, xorstr_( "E->[F+Q]" ) };
							}
						}
					}
				}

				if ( !Menu::FlashOnlyR->Value )
					return std::nullopt;

				if ( Pred::FlashW::GetSpellState() )
				{
					auto P1 = TargetSelector::GetSelectedTarget()->Position();
					if ( P1.Distance( P2 ) > Pred::EW::GetTotalRadius() )
					{
						auto R1 = Pred::EW::GetTotalRadius();
						auto R2 = Pred::FlashW::GetTotalRadius();
						if ( Pred::Q::GetSpellState() )
						{
							if ( Pred::E::GetSpellState() )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+F+W+Q->[?]" ) };
							}
							if ( Core::AI::CanDoubleCast() )
							{
								return Combo{ P1, R1, R2, xorstr_( "F+W+Q->[?]" ) };
							}
						}
						return Combo{ P1, R1, R2, xorstr_( "F+W->[?]" ) };
					}
				}

				if ( Pred::FlashQ::GetSpellState() )
				{
					if ( !Pred::W::GetSpellState() )
					{
						auto P1 = TargetSelector::GetSelectedTarget()->Position();
						if ( P1.Distance( P2 ) > Pred::EQ::GetTotalRadius() )
						{
							auto R1 = Pred::EQ::GetTotalRadius();
							auto R2 = Pred::FlashQ::GetTotalRadius();
							return Combo{ P1, R1, R2, xorstr_( "F+Q->[?]" ) };
						}
					}
				}

				return std::nullopt;
			}

			std::optional<Combo> GetGapcloseCombo( Riot::AIBaseClient* Object )
			{
				auto P2 = Riot::ObjectManager::GetPlayer()->Position();

				if ( Menu::GapEW->Value && Pred::EW::GetSpellState() )
				{
					Prediction::PredictionData Data;
					Prediction::GetPositionAfterTime( Object, Pred::EW::GetTotalDelay(), &Data );
					auto P1 = Data.Position;
					if ( P1.Distance( P2 ) > Pred::AA::GetDisplayRadius() )
					{
						auto R1 = Pred::AA::GetDisplayRadius();
						auto R2 = Pred::EW::GetTotalRadius();
						if ( Menu::R1->Value && Pred::R1::GetSpellState() )
						{
							if ( Pred::Q::GetSpellState )
							{
								if ( Menu::EAA->Value )
								{
									return Combo{ P1, R1, R2, xorstr_( "E+R1->[AA->[W+Q]]" ) };
								}

								return Combo{ P1, R1, R2, xorstr_( "E+R1->[W+Q]" ) };
							}
							if ( Menu::EAA->Value )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+R1->[AA->[W]]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E+R1->[W]" ) };
						}
						if ( Item::Crescent::GetSpellState() )
						{
							if ( Pred::Q::GetSpellState() )
							{
								if ( Menu::EAA->Value )
								{
									return Combo{ P1, R1, R2, xorstr_( "E+H->[AA->[W+Q]]" ) };
								}
								return Combo{ P1, R1, R2, xorstr_( "E+H->[W+Q]" ) };
							}
							if ( Menu::EAA->Value )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+H->[AA->[W]]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E+H->[W]" ) };
						}
						if ( Pred::Q::GetSpellState() )
						{
							if ( Menu::EAA->Value )
							{
								return Combo{ P1, R1, R2, xorstr_( "E->[AA->[W+Q]]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E->[W+Q]" ) };
						}
						if ( Menu::EAA->Value )
						{
							return Combo{ P1, R1, R2, xorstr_( "E->[AA->[W]]" ) };
						}
						return Combo{ P1, R1, R2, xorstr_( "E->[W]" ) };
					}
				}

				if ( Menu::GapEQ->Value && Pred::EQ::GetSpellState() )
				{
					Prediction::PredictionData Data;
					Prediction::GetPositionAfterTime( Object, Pred::EQ::GetTotalDelay(), &Data );
					auto P1 = Data.Position;
					if ( P1.Distance( P2 ) > Pred::AA::GetDisplayRadius() )
					{
						auto R1 = Pred::AA::GetDisplayRadius();
						auto R2 = Pred::EQ::GetTotalRadius();
						if ( Menu::R1->Value && Pred::R1::GetSpellState() )
						{
							if ( Menu::EAA->Value )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+R1->[AA->[Q]]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E+R1->[Q]" ) };
						}
						if ( Item::Crescent::GetSpellState() )
						{
							if ( Menu::EAA->Value )
							{
								return Combo{ P1, R1, R2, xorstr_( "E+H->[AA->[Q]]" ) };
							}
							return Combo{ P1, R1, R2, xorstr_( "E+H->[Q]" ) };
						}
						if ( Menu::EAA->Value )
						{
							return Combo{ P1, R1, R2, xorstr_( "E->[AA->[Q]]" ) };
						}
						return Combo{ P1, R1, R2, xorstr_( "E->[Q]" ) };
					}
				}

				if ( Menu::GapQ->Value && Pred::Q::GetSpellState() )
				{
					Prediction::PredictionData Data;
					Prediction::GetPositionAfterTime( Object, Pred::Q::GetTotalDelay(), &Data );
					auto P1 = Data.Position;
					if ( P1.Distance( P2 ) > Pred::AA::GetDisplayRadius() )
					{
						auto R1 = Pred::Q::GetDisplayRadius();
						auto R2 = Pred::AA::GetTotalRadius();
						return Combo{ P1, R1, R2, xorstr_( "Q->[?]" ) };
					}
				}

				if ( Menu::GapEAA->Value && Pred::E::GetSpellState() )
				{
					Prediction::PredictionData Data;
					Prediction::GetPositionAfterTime( Object, Pred::E::GetTotalDelay(), &Data );
					auto P1 = Data.Position;
					if ( P1.Distance( P2 ) > Pred::AA::GetDisplayRadius() )
					{
						auto R1 = Pred::E::GetDisplayRadius();
						auto R2 = Pred::AA::GetTotalRadius();
						if ( Menu::R1->Value && Pred::R1::GetSpellState() )
						{
							return Combo{ P1, R1, R2, xorstr_( "E+R1->[?]" ) };
						}
						return Combo{ P1, R1, R2, xorstr_( "E->[?]" ) };
					}
				}

				return std::nullopt;
			}

			void DrawCombo( Combo Value, std::uint32_t Color, Riot::AIBaseClient* Object )
			{
				auto P2 = ObjectManager::GetPlayer()->Position();
				auto DX = Value.Position.Distance( P2 );

				auto Q1 = Value.Position.Lerp( P2, Value.TotalRadius / DX );
				auto Q2 = Value.Position.Lerp( P2, Value.DisplayRadius / DX );

				auto Norm = ( P2 - Value.Position ).Normalized();
				auto Perp1 = Norm.Perpendicular1() * 125.f;
				auto Perp2 = Norm.Perpendicular2() * 125.f;

				auto V1 = Riot::Vector3f{ Q1.x + Perp1.x, Object->Position().y, Q1.z + Perp1.z };
				auto V2 = Riot::Vector3f{ Q1.x + Perp2.x, Object->Position().y, Q1.z + Perp2.z };

				Graphics::AddLine( V1, V2, 2, Color );

				V1.x = Q2.x + Perp1.x;
				V1.z = Q2.z + Perp1.z;

				V2.x = Q2.x + Perp2.x;
				V2.z = Q2.z + Perp2.z;

				Graphics::AddLine( V1, V2, 2, Color );

				auto T = Q1.Lerp( Q2, 0.5f );

				V1.x = T.x;
				V1.z = T.z;

				auto TextArea = Graphics::TextSize( Value.Text.c_str(), 15, true );
				auto ScreenPosition = Graphics::WorldToScreen( V1 );

				Graphics::AddText( Value.Text.c_str(), 15.f, Riot::Vector2f{ ScreenPosition.x - TextArea.x * 0.5f, ScreenPosition.y }, Color, true );
			}

			bool Flash = false;

			void GapClose()
			{
				if ( !ObjectManager::GetPlayer()->IsOnScreen() || Flash )
					return;

				const Vector3f PlayerPosition = ObjectManager::GetPlayer()->Position();
				float ClosestDistance = FLT_MAX;
				AIBaseClient* ClosestTarget = nullptr;

				if ( const auto Selected = TargetSelector::GetSelectedTarget() )
				{
					ClosestTarget = Selected;
				}
				else
				{
					for ( AIBaseClient* Target : TargetSelector::gInstance->GetOrderedTargets() )
					{
						if ( !TargetSelector::IsTargetValidNonSkillshot( Target ) )
						{
							continue;
						}

						const float Distance = PlayerPosition.DistanceSquared2D( Target->Position() );
						if ( Distance < ClosestDistance )
						{
							ClosestDistance = Distance;
							ClosestTarget = Target;
						}
					}
				}

				if ( !ClosestTarget || !ClosestTarget->IsOnScreen() )
					return;

				auto Combo = GetGapcloseCombo( ClosestTarget );

				if ( !Combo )
					return;

				DrawCombo( Combo.value(), 0xffffd1de, ClosestTarget );
			}

			void FlashCombo()
			{
				Flash = false;
				if ( TargetSelector::GetSelectedTarget() && Spell::Flash::IsReady( 0.f ) )
				{
					auto Combo = GetFlashCombo();

					if ( !Combo )
						return;

					Flash = true;

					DrawCombo( Combo.value(), 0xffffff00, TargetSelector::GetSelectedTarget() );
				}
			}

			void OnDraw()
			{
				FlashCombo();
				GapClose();
			}
		}
	}
}
