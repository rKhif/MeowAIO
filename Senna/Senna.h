#pragma once
#include "Base.h"

class SennaGlobal : public ChampionGlobalBase
{
public:
	float GetMissileSpeed(game_object_script hero, game_object_script target = nullptr) override
	{
		return FLT_MAX;
	}

	bool IgnoresSpecialWallCollision(game_object_script obj) override
	{
		return true;
	}
};