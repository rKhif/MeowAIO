#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::Push
		{
			inline Riot::AIBaseClient* Object = nullptr;
			inline float LastUpdateTick = 0.f;

			Riot::AIBaseClient* GetNearestMinionToMouse();
			std::tuple<Riot::AIBaseClient*, Riot::Vector3f> GetPrediction( float Delay, float Radius, bool UseBoundingRadius );
		}
	}
}
