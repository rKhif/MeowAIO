#include "RivenAAPred.h"

#include "OrbwalkerSDK.h"
#include "RivenPush.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::AA
		{
			std::optional<std::tuple<Riot::AIBaseClient*, Riot::Vector3f>> Result = std::nullopt;

			std::optional<std::tuple<Riot::AIBaseClient*, Riot::Vector3f>> GetPushState()
			{
				Result = std::nullopt;
				if ( !GetSpellState() )
					return Result;

				auto [ Object, Position ] = Push::GetPrediction( GetTotalDelay(), GetTotalRadius(), true );
				if ( Object )
				{
					Result = { Object, Position };
					return Result;
				}

				return std::nullopt;
			}

			Riot::AIBaseClient* GetPushResult()
			{
				if ( !Result.has_value() )
					return nullptr;

				return std::get<0>( Result.value() );
			}

			void InvokeAction( bool Pause )
			{
				Riot::ObjectManager::GetPlayer()->Attack( GetPushResult() );
				if ( Pause )
				{
					Orbwalker::SDK::SetServerPause();
				}
			}

			float GetTotalRadius()
			{
				return Riot::ObjectManager::GetPlayer()->GetAutoAttackRange();
			}

			float GetDisplayRadius()
			{
				return GetTotalRadius() + 50.f;
			}

			bool GetSpellState()
			{
				return Orbwalker::SDK::CanAttack() && !Orbwalker::SDK::IsPaused() && !Orbwalker::SDK::IsAttackPaused();
			}

			float GetTotalDelay()
			{
				return 0.f;
			}
		}
	}
}
