#pragma once
#include "SDK.h"
#include "Menu.h"
#include "BaseSpell.h"
#include "OrbwalkerData.h"
#include "TargetSelector.h"
#include "Prediction.h"
#include "DamageLib.h"
#include "Graphics.h"
#include "ChampionShared.h"
#include "NavigationMesh.h"
#include "BuffCache.h"
#include <algorithm>

namespace Features::Champions::Soraka
{
	struct HealUnit;

	namespace Menu
	{
		inline MenuSDK::CheckBox* ComboQ = nullptr;
		inline MenuSDK::CheckBox* HarassQ = nullptr;
		inline MenuSDK::Slider* MinManaQ = nullptr;
		inline MenuSDK::List* HitChanceQ = nullptr;
		inline MenuSDK::CheckBox* DrawQ = nullptr;

		inline MenuSDK::KeyBind* ToggleW = nullptr;
		inline MenuSDK::CheckBox* ComboW = nullptr;
		inline MenuSDK::CheckBox* PassiveW = nullptr;
		inline MenuSDK::Slider* MinManaW = nullptr;
		inline std::unordered_map<uint32_t, HealUnit*> WhitelistW {};
		inline MenuSDK::CheckBox* DrawW = nullptr;

		inline MenuSDK::CheckBox* ComboE = nullptr;
		inline MenuSDK::CheckBox* HarassE = nullptr;
		inline MenuSDK::MultiList* ModeE = nullptr;
		inline MenuSDK::CheckBox* DrawE = nullptr;

		inline MenuSDK::CheckBox* ToggleR = nullptr;
		inline std::unordered_map<uint32_t, MenuSDK::CheckBox*> WhitelistR {};
	}

	static constexpr float MaxRadius = 80.f;

	struct HealUnit
	{
		AIBaseClient* Client = nullptr;

		MenuSDK::CheckBox* Toggle = nullptr;
		MenuSDK::Slider* MinHP = nullptr;

		HealUnit() : LastUpdateTime(0.f) {}

		void Draw() 
		{
			Graphics::AddCircleFilled(Client->Position(), MaxRadius, Graphics::RGBA(80, 200, 120, 80));
			Graphics::AddCircle(Client->Position(), MaxRadius, 2.5f, Graphics::RGBA(80, 200, 120, 255));
			Graphics::AddCircle(Client->Position(), ComputeRadius(), 1.5f, Graphics::RGBA(80, 200, 120, 255));
		}

		[[nodiscard]] bool CanHeal() const
		{
			// Not Initialized
			if (!Toggle || !MinHP)
				return false;

			// Physically can't heal
			if (Client->IsDead() || Client->Position().Distance2D(ObjectManager::GetPlayer()->Position()) > 550.f)
				return false;

			// HP Value
			return Client->GetHealthPercentage() <= static_cast<float>(this->MinHP->Value);
		}
	private:
		float LastUpdateTime;

		float ComputeRadius() {
			constexpr float R100 = 2.f;
			constexpr float R20 = 0.75f;

			float R = R100 + (Client->GetHealthPercentage() - 100.f) * (R20 - R100) / (20.f - 100.f);
			R = std::clamp(R, R20, R100);

			const float CurrentTime = GlobalClocks::LastUpdateNetworkTime();
			const float Elapsed = CurrentTime - LastUpdateTime;
			LastUpdateTime = CurrentTime;

			const float currentRadius = MaxRadius * (1.f - Elapsed / R);
			return max(currentRadius, 0.f);
		}
	};

	inline BaseSpell Q, W, E, SnareE, R;
	inline TargetSelector::Instance* TS;

	void Initialize();
	void Destroy();
}