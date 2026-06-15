#pragma once
#include "Base.h"

class SennaSelf : public ChampionSelfBase
{
private:
	std::unordered_map<std::string, bool> soulNames = {
		{"SennaSoul", true},
	};

	std::vector<targets> specialTargets;

public:
	void OnCreateObject(game_object_script object) override
	{
		if (object->is_ally())
			return;

		if (!object->is_valid())
			return;

		if (soulNames[object->get_base_skin_name()])
		{
			specialTargets.push_back({object->get_id(), 0});
		}
	}

	void OnDeleteObject(game_object_script object) override
	{
		for (auto it = specialTargets.begin(); it != specialTargets.end(); ++it)
		{
			if (object->get_id() == it->heroId)
			{
				specialTargets.erase(it);
				return;
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::vector<game_object_script> GetSpecialTargets() override
	{
		std::vector<game_object_script> targets;
		for (auto it = specialTargets.begin(); it != specialTargets.end(); ++it)
		{
			auto target = entitylist->get_object(it->heroId);

			if (target->is_valid() &&
				target->is_valid_target()
				&& this->IsInAttackRange(target))
			{
				targets.push_back(target);
			}
		}
		return targets;
	}

	bool HasSpecialTargets() override
	{
		return Menu::GetInstance()->ChampionMenu->get_entry("Souls")->get_bool() && this->specialTargets.size() > 0;
	}

	bool IgnoreCanAttack() override
	{
		return true;
	}

	bool ShouldIgnoreCancelDetection() override
	{
		return this->HasSpecialTargets();
	}

};
