#include "riven.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "utility/hash.hpp"

namespace riven
{
	using venplus::hash::fnv1a;

	namespace
	{
		constexpr uint32_t BUFF_R1 = fnv1a("RivenFengShuiEngine");
		constexpr uint32_t BUFF_R2_READY = fnv1a("rivenwindslashready");
		constexpr float MIN_SPELL_DELAY = 0.05f;
		constexpr float Q_CHAIN_TIMEOUT = 3.9f;
		constexpr float R2_KS_HEALTH_PRED_TIME = 0.35f;

		menu_category* s_menu = nullptr;
		float last_spell_cast_time = 0.f;
		float last_q_cast_time = 0.f;
		float last_w_cast_time = 0.f;
		float last_e_cast_time = 0.f;
		float last_r_cast_time = 0.f;
		float q_chain_expire_time = 0.f;
		int q_cast_count = 0;
		bool r1_active = false;
		bool r2_active = false;

		game_object* player()
		{
			if (!g_sdk || !g_sdk->object_manager)
				return nullptr;
			return g_sdk->object_manager->get_local_player();
		}

		bool has_buff_hash(game_object* obj, uint32_t hash)
		{
			if (!obj)
				return false;
			auto* buff = obj->get_buff_by_hash(hash);
			return buff && buff->is_active();
		}

		void refresh_state()
		{
			auto* p = player();
			if (!p || !g_sdk || !g_sdk->clock_facade)
				return;

			r1_active = has_buff_hash(p, BUFF_R1);
			r2_active = has_buff_hash(p, BUFF_R2_READY);

			if (!is_spell_ready(SLOT_Q) && p->get_spell(SLOT_Q) && p->get_spell(SLOT_Q)->get_cooldown() > 1.f)
				q_cast_count = 0;

			if (q_cast_count > 0 && g_sdk->clock_facade->get_game_time() > q_chain_expire_time)
				q_cast_count = 0;
		}

		float distance_to(game_object* obj)
		{
			auto* p = player();
			if (!p || !obj)
				return FLT_MAX;
			return p->get_position().distance(obj->get_position());
		}

		float get_w_range()
		{
			return r1_active || r2_active ? spell_data::w_r_range : spell_data::w_range;
		}

		float get_q_range()
		{
			return q_cast_count >= 2 ? spell_data::q3_range : spell_data::q_range;
		}

		float get_q_radius()
		{
			return q_cast_count >= 2 ? spell_data::q3_radius : spell_data::q_radius;
		}

		float get_aa_range(game_object* target)
		{
			auto* p = player();
			if (!p)
				return 0.f;

			float range = p->get_attack_range() + p->get_bounding_radius() + 25.f;
			if (target)
				range += target->get_bounding_radius();
			return range;
		}

		bool can_cast_now()
		{
			if (!g_sdk || !g_sdk->clock_facade)
				return false;
			return g_sdk->clock_facade->get_game_time() - last_spell_cast_time >= MIN_SPELL_DELAY;
		}

		bool valid_enemy_unit(game_object* obj)
		{
			auto* p = player();
			if (!p || !obj || !obj->is_valid() || obj->is_dead() || !obj->is_targetable())
				return false;
			if (!obj->is_visible())
				return false;
			return obj->get_team_id() != p->get_team_id();
		}

		game_object* pick_hero_target(float range)
		{
			if (!sdk::target_selector)
				return nullptr;

			if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_target(forced, range))
				return forced;

			for (auto* hero : sdk::target_selector->get_sorted_heroes())
			{
				if (is_valid_target(hero, range))
					return hero;
			}

			return nullptr;
		}

		bool try_cast_q_to(const math::vector3& pos)
		{
			if (!sdk::common || !is_spell_ready(SLOT_Q) || !has_enough_resource(SLOT_Q))
				return false;

			auto* p = player();
			if (!p)
				return false;

			math::vector3 cast_pos = pos;
			const float dist = p->get_position().distance(pos);
			const float q_range = get_q_range();
			if (dist > q_range)
				cast_pos = p->get_position().extended(pos, q_range, true);

			if (!sdk::common->cast_spell(SLOT_Q, cast_pos))
				return false;

			const float now = g_sdk->clock_facade->get_game_time();
			last_q_cast_time = now;
			last_spell_cast_time = now;
			return true;
		}

		bool try_cast_q_target(game_object* target)
		{
			if (!target)
				return false;
			return try_cast_q_to(target->get_position());
		}

		bool try_cast_w(game_object* target)
		{
			if (!target || !sdk::common || !is_spell_ready(SLOT_W) || !has_enough_resource(SLOT_W))
				return false;
			if (distance_to(target) > get_w_range() + target->get_bounding_radius())
				return false;

			if (!sdk::common->cast_spell(SLOT_W))
				return false;

			const float now = g_sdk->clock_facade->get_game_time();
			last_w_cast_time = now;
			last_spell_cast_time = now;
			return true;
		}

		bool try_cast_e_to(const math::vector3& pos)
		{
			if (!sdk::common || !is_spell_ready(SLOT_E) || !has_enough_resource(SLOT_E))
				return false;

			auto* p = player();
			if (!p)
				return false;

			math::vector3 cast_pos = pos;
			const float dist = p->get_position().distance(pos);
			if (dist > spell_data::e_range)
				cast_pos = p->get_position().extended(pos, spell_data::e_range, true);

			if (!sdk::common->cast_spell(SLOT_E, cast_pos))
				return false;

			const float now = g_sdk->clock_facade->get_game_time();
			last_e_cast_time = now;
			last_spell_cast_time = now;
			return true;
		}

		bool try_cast_e_target(game_object* target)
		{
			if (!target)
				return false;
			return try_cast_e_to(target->get_position());
		}

		float get_r2_damage(game_object* target)
		{
			auto* p = player();
			if (!p || !target)
				return 0.f;

			auto* r_spell = p->get_spell(SLOT_R);
			if (!r_spell || r_spell->get_level() <= 0)
				return 0.f;

			const float missing_ratio = std::clamp(
				(target->get_max_hp() - target->get_hp()) / std::max(1.f, target->get_max_hp()),
				0.f, 1.f);
			const float multiplier = 1.f + std::min(missing_ratio * 2.67f, 2.f);
			const float raw = (50.f * static_cast<float>(r_spell->get_level())) + (0.6f * p->get_percent_bonus_physical_damage_mod());
			if (!sdk::damage)
				return raw * multiplier;
			return sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target, raw * multiplier);
		}

		pred_sdk::pred_data predict_r2(game_object* target, int hitchance)
		{
			pred_sdk::spell_data spell {};
			spell.spell_type = pred_sdk::spell_type::linear;
			spell.targetting_type = pred_sdk::targetting_type::edge_to_edge;
			spell.spell_slot = SLOT_R;
			spell.range = spell_data::r_range;
			spell.radius = spell_data::r_radius;
			spell.delay = spell_data::r_delay;
			spell.projectile_speed = spell_data::r_speed;
			spell.expected_hitchance = hitchance;
			spell.forbidden_collisions = {
				pred_sdk::collision_type::unit,
				pred_sdk::collision_type::yasuo_wall,
				pred_sdk::collision_type::braum_wall
			};

			pred_sdk::pred_data pred {};
			try
			{
				pred = sdk::prediction->predict(target, spell);
			}
			catch (...)
			{
				pred.is_valid = false;
			}
			return pred;
		}

		bool try_cast_r2(game_object* target, bool killable_only, int hitchance)
		{
			if (!target || !sdk::common || !sdk::prediction || !r2_active)
				return false;
			if (!is_spell_ready(SLOT_R) || !has_enough_resource(SLOT_R))
				return false;
			if (distance_to(target) > spell_data::r_range + target->get_bounding_radius())
				return false;

			float target_health = target->get_hp() + target->get_all_shield();
			if (sdk::health_prediction)
			{
				const float pred_hp = sdk::health_prediction->get_predicted_health(target, R2_KS_HEALTH_PRED_TIME);
				if (pred_hp > 0.f)
					target_health = std::min(target_health, pred_hp + target->get_all_shield());
			}

			if (killable_only && get_r2_damage(target) < target_health)
				return false;

			auto pred = predict_r2(target, hitchance);
			if (!pred.is_valid || pred.hitchance < hitchance)
				return false;

			if (!sdk::common->cast_spell(SLOT_R, pred.cast_position))
				return false;

			const float now = g_sdk->clock_facade->get_game_time();
			last_r_cast_time = now;
			last_spell_cast_time = now;
			return true;
		}

		bool try_cast_r1(game_object* target)
		{
			if (!target || !sdk::common || r2_active || r1_active)
				return false;
			if (!is_spell_ready(SLOT_R) || !has_enough_resource(SLOT_R))
				return false;

			const float engage_range = get_aa_range(target) + spell_data::e_range + get_q_range() + 100.f;
			if (distance_to(target) > engage_range)
				return false;

			if (!sdk::common->cast_spell(SLOT_R))
				return false;

			const float now = g_sdk->clock_facade->get_game_time();
			last_r_cast_time = now;
			last_spell_cast_time = now;
			menu::prep_r1 = false;
			return true;
		}

		void killsteal_r2()
		{
			if (!menu::ks_r2 || !r2_active || !can_cast_now())
				return;

			for (auto* hero : g_sdk->object_manager->get_heroes())
			{
				if (!is_valid_target(hero, spell_data::r_range))
					continue;
				if (try_cast_r2(hero, true, pred_sdk::hitchance::medium))
					return;
			}
		}

		void semi_r2_cast()
		{
			if (!menu::semi_r2 || !r2_active || !can_cast_now())
				return;

			auto* target = pick_hero_target(spell_data::r_range);
			if (!target)
				return;

			try_cast_r2(target, false, pred_sdk::hitchance::low);
		}

		void fast_clear_logic()
		{
			if (!sdk::orbwalker || !sdk::orbwalker->fast_clear() || !sdk::common)
				return;

			auto* p = player();
			if (!p)
				return;

			game_object* best = nullptr;
			float best_dist = FLT_MAX;

			for (auto* obj : g_sdk->object_manager->get_minions())
			{
				if (!valid_enemy_unit(obj))
					continue;
				if (!obj->is_lane_minion() && !obj->is_monster())
					continue;

				const float dist = distance_to(obj);
				if (dist < best_dist)
				{
					best_dist = dist;
					best = obj;
				}
			}

			if (!best)
				return;

			if (best_dist <= get_w_range() && is_spell_ready(SLOT_W))
			{
				try_cast_w(best);
				return;
			}

			if (best_dist <= spell_data::e_range + get_w_range() && is_spell_ready(SLOT_E))
			{
				try_cast_e_target(best);
				return;
			}

			if (best_dist <= get_q_range() + 75.f && is_spell_ready(SLOT_Q))
				try_cast_q_target(best);
		}

		float get_combo_damage(game_object* target)
		{
			auto* p = player();
			if (!p || !target || !sdk::damage)
				return 0.f;

			float total = 0.f;
			if (is_spell_ready(SLOT_Q))
				total += sdk::damage->get_spell_damage(p, target, SLOT_Q) * 3.f;
			if (is_spell_ready(SLOT_W))
				total += sdk::damage->get_spell_damage(p, target, SLOT_W);
			if (is_spell_ready(SLOT_R) && r2_active)
				total += get_r2_damage(target);

			const float aa = sdk::damage->get_aa_damage(p, target);
			total += aa * 2.f;
			return total;
		}

		int count_enemy_minions_in_range(float range, bool include_monsters)
		{
			auto* p = player();
			if (!p)
				return 0;

			int count = 0;
			for (auto* obj : g_sdk->object_manager->get_minions())
			{
				if (!valid_enemy_unit(obj))
					continue;
				if (!obj->is_lane_minion() && !(include_monsters && obj->is_monster()))
					continue;
				if (p->get_position().distance(obj->get_position()) <= range + obj->get_bounding_radius())
					++count;
			}

			return count;
		}
	}

	bool is_spell_learned(int slot)
	{
		auto* p = player();
		if (!p)
			return false;
		auto* s = p->get_spell(slot);
		return s && s->get_level() > 0;
	}

	bool is_spell_ready(int slot)
	{
		auto* p = player();
		if (!p)
			return false;
		auto* s = p->get_spell(slot);
		return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
	}

	bool has_enough_resource(int slot)
	{
		auto* p = player();
		if (!p)
			return false;
		return p->get_par() >= p->get_spell_mana_cost(slot);
	}

	bool is_casting()
	{
		auto* p = player();
		if (!p)
			return false;
		auto* active = p->get_active_spell_cast();
		if (!active)
			return false;
		auto* cast = active->get_spell_cast();
		if (!cast)
			return false;
		if (cast->is_basic_attack() || cast->is_special_attack())
			return false;
		return active->get_cast_end_time() > g_sdk->clock_facade->get_game_time();
	}

	float mana_pct()
	{
		auto* p = player();
		if (!p)
			return 0.f;
		const float max_mana = p->get_max_par();
		if (max_mana <= 0.f)
			return 0.f;
		return (p->get_par() / max_mana) * 100.f;
	}

	bool is_valid_target(game_object* unit, float range)
	{
		if (!valid_enemy_unit(unit))
			return false;
		if (range != FLT_MAX && distance_to(unit) > range + unit->get_bounding_radius())
			return false;
		return true;
	}

	void combo()
	{
		auto* p = player();
		if (!p || is_casting() || !can_cast_now())
			return;

		auto* target = pick_hero_target(spell_data::r_range);
		if (!target)
			return;

		const float dist = distance_to(target);
		const float aa_range = get_aa_range(target);

		if (menu::prep_r1 && try_cast_r1(target))
			return;

		if (menu::combo_use_r2 && r2_active)
		{
			if (try_cast_r2(target, menu::combo_r2_only_killable, pred_sdk::hitchance::medium))
				return;
		}

		if (menu::combo_use_w && dist <= get_w_range() + target->get_bounding_radius())
		{
			if (try_cast_w(target))
				return;
		}

		if (menu::combo_use_e && menu::combo_e_gapclose
			&& dist > aa_range
			&& (!menu::combo_e_outside_aa || dist > aa_range))
		{
			if (dist <= spell_data::e_range + get_w_range() + target->get_bounding_radius())
			{
				if (try_cast_e_target(target))
					return;
			}
		}

		if (menu::combo_use_q && menu::combo_q_gapclose && dist > aa_range)
		{
			if (dist <= get_q_range() + target->get_bounding_radius())
			{
				try_cast_q_target(target);
				return;
			}

			if (menu::combo_use_e && is_spell_ready(SLOT_E)
				&& dist <= spell_data::e_range + get_q_range() + target->get_bounding_radius())
			{
				if (try_cast_e_target(target))
					return;
			}
		}
	}

	void harass()
	{
		if (mana_pct() < static_cast<float>(menu::harass_mana_pct))
			return;

		auto* target = pick_hero_target(spell_data::e_range + get_q_range() + 50.f);
		if (!target || is_casting() || !can_cast_now())
			return;

		const float dist = distance_to(target);
		if (menu::harass_use_w && dist <= get_w_range() + target->get_bounding_radius())
		{
			if (try_cast_w(target))
				return;
		}

		if (menu::harass_use_e && dist <= spell_data::e_range + get_w_range() + target->get_bounding_radius())
		{
			if (try_cast_e_target(target))
				return;
		}

		if (menu::harass_use_q && dist <= get_q_range() + target->get_bounding_radius())
			try_cast_q_target(target);
	}

		void clear()
		{
			if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled())
				return;
		if (mana_pct() < static_cast<float>(menu::clear_mana_pct))
			return;
		if (is_casting() || !can_cast_now())
			return;

		fast_clear_logic();
		if (sdk::orbwalker->fast_clear() && g_sdk->clock_facade->get_game_time() - last_spell_cast_time < MIN_SPELL_DELAY)
			return;

		auto* p = player();
		if (!p)
			return;

		if (menu::clear_use_w && is_spell_ready(SLOT_W))
		{
			const int lane_count = count_enemy_minions_in_range(get_w_range(), false);
			if (lane_count >= menu::clear_w_min_minions)
			{
				sdk::common->cast_spell(SLOT_W);
				last_spell_cast_time = g_sdk->clock_facade->get_game_time();
				return;
			}
		}

		if (sdk::target_selector)
		{
			auto* monster = sdk::target_selector->get_monster_target([&](game_object* m) {
				return m && m->is_valid() && !m->is_dead() && m->is_targetable()
					&& distance_to(m) <= get_q_range() + 50.f;
			});

				if (monster)
			{
				if (menu::clear_use_w && is_spell_ready(SLOT_W)
					&& distance_to(monster) <= get_w_range() + monster->get_bounding_radius())
				{
					try_cast_w(monster);
					return;
				}

				if (menu::clear_use_q && is_spell_ready(SLOT_Q) && !sdk::orbwalker->would_cancel_attack())
				{
					try_cast_q_target(monster);
					return;
				}
			}
		}

		if (!menu::clear_use_q || !is_spell_ready(SLOT_Q) || sdk::orbwalker->would_cancel_attack())
			return;

		game_object* best = nullptr;
		float best_dist = FLT_MAX;
		for (auto* minion : g_sdk->object_manager->get_minions())
		{
			if (!valid_enemy_unit(minion) || !minion->is_lane_minion())
				continue;

			const float dist = distance_to(minion);
			if (dist > get_q_range() + minion->get_bounding_radius())
				continue;

			if (dist < best_dist)
			{
				best_dist = dist;
				best = minion;
			}
		}

		if (best)
			try_cast_q_target(best);
	}

	void flee()
	{
		auto* p = player();
		if (!p || !g_sdk || !g_sdk->hud_manager || is_casting() || !can_cast_now())
			return;

		const auto cursor = g_sdk->hud_manager->get_cursor_position();

		if (menu::flee_use_e && is_spell_ready(SLOT_E))
		{
			if (try_cast_e_to(cursor))
				return;
		}

		if (menu::flee_use_q && is_spell_ready(SLOT_Q))
			try_cast_q_to(cursor);
	}

	void __fastcall on_update()
	{
		auto* p = player();
		if (!p || p->is_dead())
			return;

		refresh_state();
		killsteal_r2();

		if (menu::semi_r2)
		{
			semi_r2_cast();
			return;
		}

		if (!sdk::orbwalker)
			return;

		if (sdk::orbwalker->combo())
			combo();
		else if (sdk::orbwalker->harass())
			harass();
		else if (sdk::orbwalker->clear() || sdk::orbwalker->fast_clear())
			clear();
		else if (sdk::orbwalker->flee())
			flee();
	}

	void __fastcall on_draw()
	{
		auto* p = player();
		if (!p || p->is_dead() || !g_sdk || !g_sdk->renderer)
			return;

		auto draw_circle = [&](float range, uint32_t color) {
			if (sdk::shaders)
				sdk::shaders->draw_world_circle(p->get_position(), range, 2.f, color);
			else
				g_sdk->renderer->add_circle_3d(p->get_position(), range, 2.f, color);
		};

		auto get_color = [&](int slot, uint32_t color) -> uint32_t {
			auto* spell = p->get_spell(slot);
			if (!spell || spell->get_level() <= 0)
				return 0u;
			if (menu::draw_only_ready && spell->get_cooldown() > 0.f)
				return 0u;
			return spell->get_cooldown() <= 0.f ? color : ((color & 0x00FFFFFFu) | 0x40000000u);
		};

		if (menu::draw_q_range)
		{
			const uint32_t color = get_color(SLOT_Q, menu::color_q);
			if (color)
				draw_circle(get_q_range(), color);
		}
		if (menu::draw_w_range)
		{
			const uint32_t color = get_color(SLOT_W, menu::color_w);
			if (color)
				draw_circle(get_w_range(), color);
		}
		if (menu::draw_e_range)
		{
			const uint32_t color = get_color(SLOT_E, menu::color_e);
			if (color)
				draw_circle(spell_data::e_range, color);
		}
		if (menu::draw_r_range && r2_active)
		{
			const uint32_t color = get_color(SLOT_R, menu::color_r);
			if (color)
				draw_circle(spell_data::r_range, color);
		}

		if (menu::draw_q_text)
		{
			const auto screen = g_sdk->renderer->world_to_screen(p->get_position());
			char buffer[64] {};
			std::snprintf(buffer, sizeof(buffer), "Q Chain: %d | R%s",
				q_cast_count,
				r2_active ? "2" : (r1_active ? "1" : ""));
			g_sdk->renderer->add_text(buffer, 14.f, { screen.x, screen.y + 40.f }, 1, 0xFFFFFFFFu);
		}

		if (menu::draw_q_timer && q_cast_count > 0)
		{
			const float remaining = std::max(0.f, q_chain_expire_time - g_sdk->clock_facade->get_game_time());
			if (remaining > 0.f)
			{
				const auto screen = g_sdk->renderer->world_to_screen(p->get_position());
				char buffer[32] {};
				std::snprintf(buffer, sizeof(buffer), "Q: %.1fs", remaining);
				const float y_offset = menu::draw_q_text ? 56.f : 40.f;
				g_sdk->renderer->add_text(buffer, 14.f, { screen.x, screen.y + y_offset }, 1, 0xFFFFFFFFu);
			}
		}

		if (menu::draw_damage_bar && sdk::damage)
		{
			for (auto* hero : g_sdk->object_manager->get_heroes())
			{
				if (!is_valid_target(hero))
					continue;
				const float dmg = get_combo_damage(hero);
				if (dmg > 0.f)
					g_sdk->renderer->add_damage_indicator(hero, dmg);
			}
		}
	}

	void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
	{
		auto* p = player();
		if (!p || !obj || !cast || obj->get_network_id() != p->get_network_id())
			return;

		if (cast->get_spell_slot() == SLOT_Q)
		{
			q_cast_count = std::min(q_cast_count + 1, 3);
			q_chain_expire_time = g_sdk->clock_facade->get_game_time() + Q_CHAIN_TIMEOUT;
			last_q_cast_time = g_sdk->clock_facade->get_game_time();
		}
		else if (cast->get_spell_slot() == SLOT_E)
		{
			last_e_cast_time = g_sdk->clock_facade->get_game_time();
		}
		else if (cast->get_spell_slot() == SLOT_W)
		{
			last_w_cast_time = g_sdk->clock_facade->get_game_time();
		}
		else if (cast->get_spell_slot() == SLOT_R)
		{
			last_r_cast_time = g_sdk->clock_facade->get_game_time();
		}
	}

	void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
	{
		auto* p = player();
		if (!p || !obj || !buff || obj->get_network_id() != p->get_network_id())
			return;

		if (buff->get_hash() == BUFF_R1)
			r1_active = true;
		else if (buff->get_hash() == BUFF_R2_READY)
			r2_active = true;
	}

	void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
	{
		auto* p = player();
		if (!p || !obj || !buff || obj->get_network_id() != p->get_network_id())
			return;

		if (buff->get_hash() == BUFF_R1)
			r1_active = false;
		else if (buff->get_hash() == BUFF_R2_READY)
			r2_active = false;
	}

	bool __stdcall on_after_attack(orb_sdk::event_data* data)
	{
		if (!data || !data->target || !sdk::orbwalker || !can_cast_now())
			return true;

		auto* target = data->target;
		if (!valid_enemy_unit(target))
			return true;

		const bool in_combo = sdk::orbwalker->combo();
		const bool in_harass = sdk::orbwalker->harass();
		const bool in_clear = sdk::orbwalker->clear() || sdk::orbwalker->fast_clear();

		if (in_harass && mana_pct() < static_cast<float>(menu::harass_mana_pct))
			return true;
		if (in_clear && mana_pct() < static_cast<float>(menu::clear_mana_pct))
			return true;

		if (target->is_hero())
		{
			if (in_combo)
			{
				if (menu::combo_use_w && try_cast_w(target))
					return true;
				if (menu::combo_use_q)
					try_cast_q_target(target);
			}
			else if (in_harass)
			{
				if (menu::harass_use_w && try_cast_w(target))
					return true;
				if (menu::harass_use_q)
					try_cast_q_target(target);
			}
			return true;
		}

		if (!in_clear || !sdk::orbwalker->is_spell_farm_enabled())
			return true;

		if ((target->is_lane_minion() || target->is_monster()) && menu::clear_use_w)
		{
			if (try_cast_w(target))
				return true;
		}

		if ((target->is_lane_minion() || target->is_monster()) && menu::clear_use_q)
			try_cast_q_target(target);

		return true;
	}

	void load()
	{
		if (!g_sdk || !g_sdk->menu_manager)
			return;

		s_menu = g_sdk->menu_manager->add_category("riven", "MeowAIO - Riven");
		if (!s_menu)
		{
			g_sdk->log_console("[!] Riven: failed to create menu category");
			return;
		}

		auto* q_menu = s_menu->add_sub_category("q_broken_wings", "Q - Broken Wings");
		q_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_q,
			[](bool v) { menu::combo_use_q = v; });
		q_menu->add_checkbox("combo_gapclose", "Use to Gapclose", menu::combo_q_gapclose,
			[](bool v) { menu::combo_q_gapclose = v; });
		q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q,
			[](bool v) { menu::harass_use_q = v; });
		q_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_q,
			[](bool v) { menu::clear_use_q = v; });
		q_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_q,
			[](bool v) { menu::flee_use_q = v; });
		q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
			[](bool v) { menu::draw_q_range = v; });

		auto* w_menu = s_menu->add_sub_category("w_ki_burst", "W - Ki Burst");
		w_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_w,
			[](bool v) { menu::combo_use_w = v; });
		w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w,
			[](bool v) { menu::harass_use_w = v; });
		w_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_w,
			[](bool v) { menu::clear_use_w = v; });
		w_menu->add_slider_int("clear_min", "Lane Clear Min Minions", 1, 6, 1, menu::clear_w_min_minions,
			[](int v) { menu::clear_w_min_minions = v; });
		w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
			[](bool v) { menu::draw_w_range = v; });

		auto* e_menu = s_menu->add_sub_category("e_valor", "E - Valor");
		e_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_e,
			[](bool v) { menu::combo_use_e = v; });
		e_menu->add_checkbox("gapclose", "Gapclose", menu::combo_e_gapclose,
			[](bool v) { menu::combo_e_gapclose = v; });
		e_menu->add_checkbox("outside_aa", "Only Outside AA Range", menu::combo_e_outside_aa,
			[](bool v) { menu::combo_e_outside_aa = v; });
		e_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_e,
			[](bool v) { menu::harass_use_e = v; });
		e_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_e,
			[](bool v) { menu::flee_use_e = v; });
		e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
			[](bool v) { menu::draw_e_range = v; });

		auto* r_menu = s_menu->add_sub_category("r_blade", "R - Blade of the Exile / Wind Slash");
		r_menu->add_checkbox("combo_r2", "Use R2 in Combo", menu::combo_use_r2,
			[](bool v) { menu::combo_use_r2 = v; });
		r_menu->add_checkbox("killable_only", "R2 Only if Killable", menu::combo_r2_only_killable,
			[](bool v) { menu::combo_r2_only_killable = v; });
		r_menu->add_checkbox("ks", "Killsteal with R2", menu::ks_r2,
			[](bool v) { menu::ks_r2 = v; });
		r_menu->add_hotkey("prep_r1", "Prep R1 for Next Combo", 'C', false, true,
			[](std::string* hk, bool active) {
				menu::prep_r1 = active;
				if (hk) menu::prep_r1_key_str = *hk;
			});
		r_menu->add_hotkey("semi_r2", "Semi-Manual R2", 'T', false, false,
			[](std::string* hk, bool pressed) {
				menu::semi_r2 = pressed;
				if (hk) menu::semi_r2_key_str = *hk;
			});
		r_menu->add_checkbox("draw_range", "Draw R2 Range", menu::draw_r_range,
			[](bool v) { menu::draw_r_range = v; });

		auto* mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
		mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
			[](int v) { menu::harass_mana_pct = v; });
		mana_menu->add_slider_int("clear", "Clear Mana %", 0, 100, 5, menu::clear_mana_pct,
			[](int v) { menu::clear_mana_pct = v; });

		auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings");
		draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
			[](bool v) { menu::draw_only_ready = v; });
		draw_menu->add_checkbox("damage", "Combo Damage Indicator", menu::draw_damage_bar,
			[](bool v) { menu::draw_damage_bar = v; });
		draw_menu->add_checkbox("q_text", "Draw Q Chain Text", menu::draw_q_text,
			[](bool v) { menu::draw_q_text = v; });
		draw_menu->add_checkbox("q_timer", "Draw Q Timer", menu::draw_q_timer,
			[](bool v) { menu::draw_q_timer = v; });
		draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
			[](uint32_t c) { menu::color_q = c; });
		draw_menu->add_colorpicker("col_w", "W Color", menu::color_w,
			[](uint32_t c) { menu::color_w = c; });
		draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
			[](uint32_t c) { menu::color_e = c; });
		draw_menu->add_colorpicker("col_r", "R Color", menu::color_r,
			[](uint32_t c) { menu::color_r = c; });

		g_sdk->event_manager->register_callback(event_manager::event::game_update,
			reinterpret_cast<void*>(on_update));
		g_sdk->event_manager->register_callback(event_manager::event::draw_world,
			reinterpret_cast<void*>(on_draw));
		g_sdk->event_manager->register_callback(event_manager::event::process_cast,
			reinterpret_cast<void*>(on_process_cast));
		g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
			reinterpret_cast<void*>(on_buff_gain));
		g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
			reinterpret_cast<void*>(on_buff_loss));

		if (sdk::orbwalker)
		{
			sdk::orbwalker->register_callback(orb_sdk::event_type::after_attack,
				reinterpret_cast<void*>(on_after_attack));
		}

		if (sdk::infotab)
		{
			menu::infotab_prep_r1 = sdk::infotab->add_hotkey_text(
				&menu::prep_r1_key_str, { "Prep R1", 0xFFFFFFFFu },
				[]() -> infotab_sdk::text_entry {
					return { menu::prep_r1 ? "ON" : "OFF",
						menu::prep_r1 ? 0xFF00FF00u : 0xFFFF0000u };
				});

			menu::infotab_semi_r2 = sdk::infotab->add_hotkey_text(
				&menu::semi_r2_key_str, { "Semi R2", 0xFFFFFFFFu },
				[]() -> infotab_sdk::text_entry {
					return { menu::semi_r2 ? "ON" : "OFF",
						menu::semi_r2 ? 0xFF00FF00u : 0xFFFF0000u };
				});
		}
	}

	void unload()
	{
		if (g_sdk && g_sdk->event_manager)
		{
			g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
				reinterpret_cast<void*>(on_update));
			g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
				reinterpret_cast<void*>(on_draw));
			g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
				reinterpret_cast<void*>(on_process_cast));
			g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
				reinterpret_cast<void*>(on_buff_gain));
			g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
				reinterpret_cast<void*>(on_buff_loss));
		}

		if (sdk::orbwalker)
		{
			sdk::orbwalker->unregister_callback(orb_sdk::event_type::after_attack,
				reinterpret_cast<void*>(on_after_attack));
		}

		if (sdk::infotab)
		{
			if (menu::infotab_prep_r1 != 0)
			{
				sdk::infotab->remove_text(menu::infotab_prep_r1);
				menu::infotab_prep_r1 = 0;
			}
			if (menu::infotab_semi_r2 != 0)
			{
				sdk::infotab->remove_text(menu::infotab_semi_r2);
				menu::infotab_semi_r2 = 0;
			}
		}

		s_menu = nullptr;
		last_spell_cast_time = 0.f;
		last_q_cast_time = 0.f;
		last_w_cast_time = 0.f;
		last_e_cast_time = 0.f;
		last_r_cast_time = 0.f;
		q_chain_expire_time = 0.f;
		q_cast_count = 0;
		r1_active = false;
		r2_active = false;
		menu::prep_r1 = false;
		menu::semi_r2 = false;
	}
}
