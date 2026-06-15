#pragma once
#include "../../plugin_sdk/plugin_sdk.hpp"
#include "../../Input/Input.h"
#include "../../Tracker/Tracker.h"
#include "../../Helper/Helper.h"
#include "../../Menu/Menu.h"
#include "../../DamageLib/DamageLib.h"
#include "../../Prediction/Prediction.h"

struct targets
{

	uint32_t heroId;
	uint32_t particleId;
};

class ChampionSelfBase
{
public:
	virtual bool IsInAttackRange(game_object_script target)
	{
		float offset    = myhero->get_bounding_radius() + target->get_bounding_radius();

		if (myhero->get_distance(target) > myhero->get_attack_range() + offset)
			return false;

		if (target->is_ai_turret() || target->is_ward())
			return true;

		auto predictedPosition = Prediction::GetInstance()->PredictUnitPosition(target, 2.f / 30.f);

		return myhero->get_position().distance(predictedPosition) <= myhero->get_attack_range() + offset;
	}

	virtual float GetAutoAttackRange()
	{
		return myhero->get_attack_range() + myhero->get_bounding_radius();
	}

	virtual bool HasSpecialTargets()
	{
		return false;
	}

	virtual std::vector<game_object_script> GetSpecialTargets()
	{
		return {};
	}

	virtual bool IgnoreBlockOrders()
	{
		return false;
	}

	virtual bool IsCanAttackOverriden()
	{
		return false;
	}

	virtual bool IsCanMoveOverriden()
	{
		return false;
	}


	virtual bool IgnoreCanAttack()
	{
		return false;
	}

	virtual bool ShouldIgnoreCancelDetection()
	{
		return false;
	}

	virtual bool IgnoreDeadTime()
	{
		return false;
	}

	virtual bool ShouldProcFakeAttack()
	{
		return false;
	}

	virtual float ProcFakeAttackModifier()
	{
		return 1.f;
	}

	virtual bool CanIgnoreDashing()
	{
		return false;
	}

	virtual bool CanIgnoreWindup()
	{
		return false;
	}

	virtual bool CanMove(Tracker* tracker, Input* input)
	{
		return false;
	}

	virtual bool CanAttack(Tracker* tracker, Input* input)
	{
		return false;
	}

	virtual bool IgnoresSpecialWallCollision()
	{
		return false;
	}

	// events

	virtual void OnUpdate()
	{
		return;
	}

	virtual void OnCreateObject(game_object_script object)
	{
		return;
	}

	virtual void OnDeleteObject(game_object_script object)
	{
		return;
	}

	virtual void OnIssueOrder(game_object_script& target, vector& pos, _issue_order_type& type, bool* process)
	{
		return;
	}

	virtual void OnPlayAnimation(game_object_script sender, const char* name, bool* process)
	{
		return;
	}

	virtual void OnBasicAttack(game_object_script sender, spell_instance_script spell)
	{
		return;
	}
	
	virtual void OnProcessSpell(game_object_script sender, spell_instance_script spell)
	{
		return;
	}

	virtual void OnDraw()
	{
		return;
	}

	virtual void OnDrawPost()
	{
		return;
	}
	
};