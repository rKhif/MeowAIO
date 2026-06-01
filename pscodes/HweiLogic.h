#pragma once
#include "OrbwalkerLogic.h"

namespace Features::Orbwalker
{
	class HweiLogicImpl : public OrbwalkerLogicBase
	{
	public:
		HweiLogicImpl( std::uint32_t NetworkID, std::shared_ptr<AttackData> Data );
		float GetProjectileSpeed() const override;
	};

	class HweiLogic : public OrbwalkerLogic
	{
	public:
		HweiLogic( Riot::AIBaseClient* unit );
		OrbwalkerLogicBase* GetLogic() const override;
	};
}
