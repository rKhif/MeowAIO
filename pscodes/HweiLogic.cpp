#include "HweiLogic.h"
#include "BuffCache.h"
#include "OrbwalkerData.h"

namespace Features::Orbwalker
{
	HweiLogicImpl::HweiLogicImpl( std::uint32_t NetworkID, std::shared_ptr<AttackData> Data )
	{
		this->NetworkID = NetworkID;
		this->Data = Data;
	}

	float HweiLogicImpl::GetProjectileSpeed() const
	{
		return 2800.f;
	}

	HweiLogic::HweiLogic( AIBaseClient* unit )
	{
		this->NetworkID = unit->NetworkID();
		this->Data = std::make_shared<AttackData>();
	}

	OrbwalkerLogicBase* HweiLogic::GetLogic() const
	{
		return new HweiLogicImpl( this->NetworkID, this->Data );
	}
}
