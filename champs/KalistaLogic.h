#pragma once
#include "OrbwalkerLogic.h"

namespace Features::Orbwalker
{
	class KalistaLogicImpl : public OrbwalkerLogicBase
	{
	public:
		KalistaLogicImpl( std::uint32_t NetworkID, std::shared_ptr<AttackData> Data );
		[[nodiscard]] bool CanAttack( float delta = 0.f ) const override;
	};

	class KalistaLogic : public OrbwalkerLogic
	{
	public:
		KalistaLogic( Riot::AIBaseClient* unit );
		[[nodiscard]] OrbwalkerLogicBase* GetLogic() const override;
	};
}
