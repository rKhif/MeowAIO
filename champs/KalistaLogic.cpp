#include "KalistaLogic.h"

#include "OrbwalkerData.h"

namespace Features::Orbwalker
{
	KalistaLogicImpl::KalistaLogicImpl( std::uint32_t NetworkID, std::shared_ptr<AttackData> Data )
	{
		this->NetworkID = NetworkID;
		this->Data = Data;
	}

	bool KalistaLogicImpl::CanAttack( float delta ) const
	{
		const auto& Unit = this->GetUnit();

		if ( !Unit )
			return false;

		if ( !Unit->mCharacterState()->CanAttack() )
			return false;

		return OrbwalkerLogicBase::CanAttack( delta );
	}

	KalistaLogic::KalistaLogic( AIBaseClient* unit )
	{
		this->NetworkID = unit->NetworkID();
		this->Data = std::make_shared<AttackData>();
	}

	OrbwalkerLogicBase* KalistaLogic::GetLogic() const
	{
		return new KalistaLogicImpl( this->NetworkID, this->Data );
	}
}
