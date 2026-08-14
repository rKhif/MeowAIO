#include "veigar.h"

#include "utility/defensive_buffs.hpp"
#include "utility/minion_manager.hpp"
#include "math/geometry.hpp"
#include "damage/calc.hpp"               // raw_spell_damage, final_spell_damage, effective_resist
#include "damage/items.hpp"
#include "db/champions/veigar.hpp"       // canonical VEIGAR damage entries per slot

#include <algorithm>
#include <cfloat>
#include <vector>
#include <string>

namespace veigar
{
	static float last_cast_e = 0.f;
	static bool  anti_gap_fired = false;

	static float m_last_q_cast_time = 0.f;
	static float m_last_w_cast_time = 0.f;
	static float m_last_e_cast_time = 0.f;
	static constexpr float MIN_SPELL_DELAY = 0.06f; // 60ms per-spell gate

	static struct {
		math::vector3 center{};
		float         end_time = 0.f;
		bool          active = false;
	} e_wall_track;

	static struct {
		math::vector3 pos{};
		float         impact_time = 0.f;
		bool          active = false;
	} w_track;

	static float get_cc_remaining(game_object* target)
	{
		if (!target || !g_sdk || !g_sdk->clock_facade) return 0.f;
		const float now = g_sdk->clock_facade->get_game_time();
		float best = 0.f;
		for (auto* b : target->get_buffs())
		{
			if (!b || !b->is_active()) continue;
			const auto t = b->get_type();
			if (t != buff_type::stun && t != buff_type::snare &&
				t != buff_type::knockup && t != buff_type::knockback &&
				t != buff_type::charm && t != buff_type::suppression &&
				t != buff_type::taunt && t != buff_type::asleep &&
				t != buff_type::fear && t != buff_type::flee)
				continue;
			const float remaining = b->get_end_time() - now;
			if (remaining > best) best = remaining;
		}
		return best;
	}

	static inline bool is_target_immobile(game_object* target)
	{
		return get_cc_remaining(target) > 0.f;
	}

	// True if hero is inside our active E cage (within the inner safe zone).
	static inline bool is_in_our_cage(game_object* hero, float now)
	{
		if (!e_wall_track.active || now >= e_wall_track.end_time) return false;
		return e_wall_track.center.distance(hero->get_position()) < spell_data::e_inner_radius;
	}

	static inline bool is_valid_minion_target(game_object* o, int player_team)
	{
		return o && o->is_valid() && !o->is_dead()
			&& o->is_targetable()
			&& o->get_team_id() != player_team;
	}

	static inline venplus::damage::caster_stats make_caster_stats(game_object* p)
	{
		return {
			.ap       = p->get_ability_power(),
			.bonus_ad = p->get_attack_damage() - p->get_base_attack_damage(),
			.total_ad = p->get_attack_damage(),
			.level    = static_cast<float>(p->get_level()),
		};
	}
	static inline venplus::damage::target_info make_target_info(game_object* t)
	{
		return {
			.max_hp      = t->get_max_hp(),
			.curr_hp     = t->get_hp(),
			.armor       = t->get_armor(),
			.bonus_armor = t->get_bonus_armor(),
			.mr          = t->get_magic_resist(),
			.bonus_mr    = t->get_bonus_magic_resist(),
		};
	}
	static inline venplus::damage::pen_stats make_magic_pen(game_object* p)
	{
		return {
			.pct_bonus_pen = p->get_percent_bonus_magic_penetration(),
			.pct_total_pen = p->get_percent_magic_penetration(),
			.flat_pen      = p->get_flat_magic_penetration(),
		};
	}

	bool is_spell_learned(int slot)
	{
		if (!g_sdk || !g_sdk->object_manager) return false;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return false;

		auto sp = player->get_spell(slot);
		if (!sp) return false;
		return sp->get_level() > 0;
	}

	bool is_spell_ready(int slot)
	{
		if (!g_sdk || !g_sdk->object_manager) return false;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return false;

		auto sp = player->get_spell(slot);
		if (!sp) return false;
		return sp->get_level() > 0 && sp->get_cooldown() <= 0.f;
	}

	bool has_enough_mana(int slot)
	{
		if (!g_sdk || !g_sdk->object_manager) return false;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return false;

		const float current_mana = player->get_par();
		const float cost = player->get_spell_mana_cost(slot);
		return current_mana >= cost;
	}

	bool is_casting()
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return false;

		auto active_cast = player->get_active_spell_cast();
		if (active_cast && active_cast->get_spell_cast())
		{
			const float now = g_sdk->clock_facade->get_game_time();
			const float end_time = active_cast->get_cast_end_time();
			if (end_time > now) return true;
		}

		return false;
	}

	inline uint32_t fade_color(uint32_t color) noexcept
	{
		return (color & 0x00FFFFFFu) | 0x40000000u;
	}

	// UI wiring lives at the bottom of the file.
	static void setup_menu();
	static void setup_infotab();

	void load()
	{
		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->get_char_name() != "Veigar")
			return;

		g_sdk->log_console("[MeowAIO] Loading Veigar >.<");

		g_sdk->event_manager->register_callback(event_manager::event::game_update,
			reinterpret_cast<void*>(on_update));
		g_sdk->event_manager->register_callback(event_manager::event::draw_world,
			reinterpret_cast<void*>(on_draw));
		g_sdk->event_manager->register_callback(event_manager::event::present,
			reinterpret_cast<void*>(on_present));
		g_sdk->event_manager->register_callback(event_manager::event::process_cast,
			reinterpret_cast<void*>(on_process_cast));

		if (sdk::orbwalker)
			sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
				reinterpret_cast<void*>(on_before_attack));

		setup_menu();
		setup_infotab();

		g_sdk->log_console("[MeowAIO] Veigar loaded successfully :3");
	}

	void unload()
	{
		if (g_sdk && g_sdk->event_manager)
		{
			g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
				reinterpret_cast<void*>(on_update));
			g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
				reinterpret_cast<void*>(on_draw));
			g_sdk->event_manager->unregister_callback(event_manager::event::present,
				reinterpret_cast<void*>(on_present));
			g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
				reinterpret_cast<void*>(on_process_cast));
		}

		if (sdk::orbwalker)
			sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
				reinterpret_cast<void*>(on_before_attack));

		if (sdk::infotab)
		{
			if (menu::infotab_auto_q_farm) sdk::infotab->remove_text(menu::infotab_auto_q_farm);
			if (menu::infotab_save_r)      sdk::infotab->remove_text(menu::infotab_save_r);
			if (menu::infotab_semi_w) sdk::infotab->remove_text(menu::infotab_semi_w);
			if (menu::infotab_semi_e) sdk::infotab->remove_text(menu::infotab_semi_e);
			if (menu::infotab_semi_e_edge) sdk::infotab->remove_text(menu::infotab_semi_e_edge);
		}

		last_cast_e = 0.f;
		anti_gap_fired = false;
		m_last_q_cast_time = m_last_w_cast_time = m_last_e_cast_time = 0.f;
		e_wall_track = {};
		w_track = {};

		g_sdk->log_console("[MeowAIO] Veigar unloaded successfully :3");
	}

	void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
	{
		if (!obj || !cast || !g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || obj != player) return;

		const int slot = cast->get_spell_slot();

		// E cast — record cage for drawing
		if (slot == SLOT_E)
		{
			const auto cast_pos = cast->get_end_pos();
			if (cast_pos.x != 0.f || cast_pos.y != 0.f || cast_pos.z != 0.f)
			{
				const float now = g_sdk->clock_facade->get_game_time();
				e_wall_track.center = cast_pos;
				e_wall_track.end_time = now + 3.f; // E lasts 3 seconds
				e_wall_track.active = true;
			}
		}

		// W cast — record landing for Q-after-W combo
		if (slot == SLOT_W)
		{
			const auto cast_pos = cast->get_end_pos();
			if (cast_pos.x != 0.f || cast_pos.y != 0.f || cast_pos.z != 0.f)
			{
				const float now = g_sdk->clock_facade->get_game_time();
				w_track.pos = cast_pos;
				w_track.impact_time = now + spell_data::w_delay;
				w_track.active = true;
			}
		}
	}

	void __fastcall on_update()
	{
		anti_gap_fired = false;

		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade)
			return;

		if (g_sdk->is_chat_open() || g_sdk->is_shop_open())
			return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead())
			return;

		killsteal();
		stasis_e();
		anti_gap_e();
		auto_w_on_cc();
		auto_w_in_cage();
		auto_e_on_stun();

		// Semi-manual W
		if (menu::semi_w_active && sdk::target_selector)
		{
			game_object* target = sdk::target_selector->get_forced_target();
			if (!target || !is_valid_target(target, spell_data::w_range))
				target = sdk::target_selector->get_hero_target();
			if (target && is_valid_target(target, spell_data::w_range))
				cast_w(target, true);
		}

		// Semi-manual E center — keep cage centered on target, no edge-stun shift.
		if (menu::semi_e_active && sdk::target_selector)
		{
			game_object* target = sdk::target_selector->get_forced_target();
			if (!target || !is_valid_target(target, spell_data::e_range))
				target = sdk::target_selector->get_hero_target();
			if (target && is_valid_target(target, spell_data::e_range))
				cast_e(target, /*anti_gapclose=*/false, /*force=*/true, /*wall_on_target=*/false);
		}

		// Semi-manual E edge stun
		if (menu::semi_e_edge_active && sdk::target_selector)
		{
			const float e_reach = spell_data::e_range + spell_data::e_outer_radius;
			game_object* target = sdk::target_selector->get_forced_target();
			if (!target || !is_valid_target(target, e_reach))
				target = sdk::target_selector->get_hero_target();
			if (target && is_valid_target(target, e_reach))
				cast_e_edge_stun(target, true);
		}

		if (!sdk::orbwalker) return;

		// Auto Q Farm: run even outside orbwalker lasthit/clear modes.
		if (menu::auto_q_farm
			&& !sdk::orbwalker->combo()
			&& !sdk::orbwalker->harass()
			&& !sdk::orbwalker->lasthit()
			&& !sdk::orbwalker->clear())
		{
			lasthit(true);
		}

		if (sdk::orbwalker->combo()) combo();
		else if (sdk::orbwalker->harass()) harass();
		else if (sdk::orbwalker->lasthit()) lasthit();
		else if (sdk::orbwalker->clear()) clear();
	}

	void __fastcall on_draw()
	{
		if (!g_sdk || !g_sdk->object_manager)
			return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead())
			return;

		const auto player_pos = player->get_position();

		auto draw_range = [&](int slot, bool enabled, uint32_t clr, float range_override = 0.f)
			{
				if (!enabled) return;
				auto sp = player->get_spell(slot);
				if (!sp || sp->get_level() == 0) return;
				const bool     ready = sp->get_cooldown() <= 0.f;
				if (menu::draw_only_ready && !ready) return;
				const float    range = range_override > 0.f ? range_override : sp->get_cast_range();
				const uint32_t draw_color = ready ? clr : fade_color(clr);
				if (sdk::shaders)
					sdk::shaders->draw_world_circle(player_pos, range, 2.f, draw_color);
				else if (g_sdk->renderer)
					g_sdk->renderer->add_circle_3d(player_pos, range, 2.f, draw_color);
			};

		draw_range(SLOT_Q, menu::draw_q_range, menu::color_q, spell_data::q_range);
		draw_range(SLOT_W, menu::draw_w_range, menu::color_w, spell_data::w_range);
		draw_range(SLOT_E, menu::draw_e_range, menu::color_e, spell_data::e_range);
		draw_range(SLOT_R, menu::draw_r_range, menu::color_r, spell_data::r_range);

		// "R KILLABLE" text
		if (g_sdk->renderer && menu::draw_r_killable && g_sdk->clock_facade)
		{
			auto r_sp = player->get_spell(SLOT_R);
			if (r_sp && r_sp->get_level() > 0 && r_sp->get_cooldown() <= 0.f)
			{
				const float now_draw = g_sdk->clock_facade->get_game_time();
				for (auto hero : g_sdk->object_manager->get_heroes())
				{
					if (!is_valid_target(hero)) continue;
					float hp = hero->get_hp();
					if (sdk::health_prediction)
					{
						try
						{
							const float r_dist   = player->get_position().distance(hero->get_position());
							const float r_travel = venplus::math::travel_time(r_dist, spell_data::r_missile_speed, spell_data::r_cast_time);
							const float pred = sdk::health_prediction->get_predicted_health(hero, now_draw + r_travel);
							if (pred > 0.f && pred <= hero->get_max_hp() && pred <= hp) hp = pred;
						}
						catch (...) {}
					}
					if (hp > 0.f && hp <= get_r_damage(hero))
					{
						auto screen = g_sdk->renderer->world_to_screen(hero->get_position());
						g_sdk->renderer->add_text("R KILLABLE", 18.f,
							{ screen.x - 35.f, screen.y - 60.f }, 0, 0xFFFF4500u);
					}
				}
			}
		}

		// "SAVE R" status indicator above Veigar — only shown when R is learned
		{
			auto r_sp = player->get_spell(SLOT_R);
			if (g_sdk->renderer && r_sp && r_sp->get_level() > 0)
			{
				auto screen = g_sdk->renderer->world_to_screen(player_pos);
				if (menu::save_r)
					g_sdk->renderer->add_text("SAVE R: ON", 16.f, { screen.x - 38.f, screen.y - 85.f }, 0, 0xFF00FF00u);
				else
					g_sdk->renderer->add_text("SAVE R: OFF", 16.f, { screen.x - 40.f, screen.y - 85.f }, 0, 0xFFFF0000u);
			}
		}

		// Active E cage rings — only while E wall is up
		if (menu::draw_e_cage && g_sdk->renderer && e_wall_track.active && g_sdk->clock_facade)
		{
			const float now_e = g_sdk->clock_facade->get_game_time();
			if (now_e < e_wall_track.end_time)
			{
				g_sdk->renderer->add_circle_3d(e_wall_track.center, spell_data::e_outer_radius, 2.f, 0xFFFF0000u);
				g_sdk->renderer->add_circle_3d(e_wall_track.center, spell_data::e_inner_radius, 2.f, 0xFFFFFF00u);
			}
			else
			{
				e_wall_track.active = false;
			}
		}

		// Passive stacks: icon + count below champion
		if (menu::draw_stacks && g_sdk->renderer)
		{
			const auto passive_sp = player->get_spell(63); // slot 63 = passive
			if (passive_sp)
			{
				int stacks = 0;
				{
					auto* sp_data = passive_sp->get_data();
					if (sp_data)
					{
						void* icon = sp_data->get_icon();
						const auto screen_s = g_sdk->renderer->world_to_screen(player_pos);
						math::vector2 sprite_pos = screen_s;
						sprite_pos.y += 30.f;

						uint32_t stack_color = 0xFFFFFFFFu;
						stacks = static_cast<int>(passive_sp->get_charges());

						if (stacks >= 200)      stack_color = 0xFF00FF00u; // green
						else if (stacks >= 100)  stack_color = 0xFFFFFF00u; // yellow

						if (icon)
						{
							g_sdk->renderer->add_sprite(icon, sprite_pos, 0xFFFFFFFFu, { 0.35f, 0.35f });
							math::vector2 text_pos = sprite_pos;
							text_pos.x += 18.f;
							g_sdk->renderer->add_text(std::to_string(stacks), 12.f, text_pos, 0, stack_color);
						}
						else
						{
							g_sdk->renderer->add_text(std::to_string(stacks), 12.f, sprite_pos, 0, stack_color);
						}
					}
				}
			}
		}

	}

	void __fastcall on_present()
	{
		// Minimal checks; get_r_damage() handles the rest.
		if (!g_sdk || !g_sdk->object_manager)
			return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !player->is_valid() || player->is_dead())
			return;

		if (!menu::draw_r_damage_indicator)
			return;

		auto r_sp = player->get_spell(SLOT_R);
		if (!r_sp || r_sp->get_level() <= 0)
			return;

		if (r_sp->get_cooldown() > 0.f)
			return;

		if (!g_sdk->renderer)
			return;

		const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
		const float ox = -45.6f * scale, oy = 15.5f * scale;
		const float bw = 106.4f * scale, bh = 11.5f * scale;

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!hero || !hero->is_valid() || hero->is_dead())
				continue;
			if (hero->get_team_id() == player->get_team_id())
				continue;
			if (!hero->is_visible())
				continue;

			const float r_damage = get_r_base_damage(hero); // base only — no execute bonus on HP bar
			const float hp = hero->get_hp(), mhp = hero->get_max_hp();
			if (r_damage <= 0.f || mhp <= 0.f || hp <= 0.f) continue;

			const auto hbar = hero->get_health_bar_position();
			if (hbar.x < 1.f && hbar.y < 1.f) continue;

			const float r0 = hp / mhp;
			const float r1 = std::max(0.f, hp - r_damage) / mhp;
			if (r0 <= r1) continue;

			math::rect rc;
			rc.left   = hbar.x + ox + r1 * bw;
			rc.top    = hbar.y + oy;
			rc.right  = hbar.x + ox + r0 * bw;
			rc.bottom = hbar.y + oy + bh;
			g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
		}
	}

	static game_object* get_best_target(float max_range)
	{
		if (!g_sdk || !g_sdk->object_manager) return nullptr;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return nullptr;

		game_object* target = nullptr;

		// Forced target wins; otherwise hero_target; otherwise nearest valid.
		if (sdk::target_selector)
		{
			target = sdk::target_selector->get_forced_target();
			if (target && is_valid_target(target, max_range))
				return target;
			target = sdk::target_selector->get_hero_target();
		}
		if (target && is_valid_target(target, max_range))
			return target;

		float best_dist = max_range;
		target = nullptr;
		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero, max_range)) continue;
			const float d = player->get_position().distance(hero->get_position());
			if (d < best_dist)
			{
				best_dist = d;
				target = hero;
			}
		}

		return target;
	}

	bool is_valid_target(game_object* unit, float range)
	{
		if (!unit || !unit->is_valid() || unit->is_dead()) return false;
		if (!unit->is_visible() || !unit->is_targetable())  return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return false;
		if (unit->get_team_id() == player->get_team_id()) return false;

		if (range != FLT_MAX)
		{
			if (player->get_position().distance(unit->get_position()) > range)
				return false;
		}
		return true;
	}

	void combo()
	{
		if (!sdk::orbwalker) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		if (is_casting()) return;

		// E range + outer radius covers edge-stun placement.
		const float max_spell_range = spell_data::e_range + spell_data::e_outer_radius;

		auto target = get_best_target(max_spell_range);
		if (!target || !is_valid_target(target)) return;

		// Priority: R (kill secure) → Q → W (only on CC/in-cage) → Q-after-W.
		if (menu::use_r_combo && !is_casting() && cast_r(target)) return;
		if (menu::use_q_combo && !is_casting() && cast_q(target)) return;

		// W only when target can't escape — free-moving targets waste the spell.
		if (menu::use_w_combo && !anti_gap_fired && !is_casting())
		{
			const float now_combo = g_sdk->clock_facade->get_game_time();
			if ((is_target_immobile(target) || is_in_our_cage(target, now_combo))
				&& cast_w(target)) return;
		}

		if (!is_casting()) cast_q_after_w();
	}

	void harass()
	{
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return;

		const float max_par = player->get_max_par();
		if (max_par <= 0.f) return;
		if ((player->get_par() / max_par) * 100.f < static_cast<float>(menu::harass_mana_percent))
			return;

		if (is_casting()) return;
		const float max_spell_range = spell_data::q_range;

		auto target = get_best_target(max_spell_range);
		if (!target || !is_valid_target(target)) return;

		if (menu::use_q_harass) if (cast_q(target)) return;
		if (menu::use_w_harass)     cast_w(target);
	}

	// Helper function to check if a minion is lasthittable with Q
	static bool is_minion_lasthittable(game_object* minion, game_object* player, float q_range, float now, float& out_hp, float& out_score)
	{
		if (!is_valid_minion_target(minion, player->get_team_id())) return false;
		// Lane minions only — unit_class filters plants/pets/walls/clones in one switch.
		const auto cls = venplus::classify_unit(minion);
		if (!venplus::db::minions::is_lane_minion(cls)) return false;

		const auto ppos = player->get_position();
		const auto mpos = minion->get_position();
		const float dist = ppos.distance(mpos);
		if (dist > q_range) return false;

		float hp = minion->get_hp();
		const float q_dmg = get_q_damage(minion);
		if (q_dmg <= 0.f) return false;

		// SDK health_prediction accounts for turret + allied-minion incoming damage.
		if (sdk::health_prediction && g_sdk->clock_facade)
		{
			try
			{
				const float prediction_time = now + spell_data::q_delay + dist / spell_data::q_speed;
				float predicted_hp = sdk::health_prediction->get_predicted_health(minion, prediction_time);
				if (predicted_hp > 0.f && predicted_hp <= minion->get_max_hp() && predicted_hp <= hp
					&& (hp - predicted_hp) > prediction_constants::min_hp_difference)
					hp = predicted_hp;
			}
			catch (...) {}
		}

		// 25% margin: only fire Q when HP ≤ 75% of calculated damage. Compensates
		const float effective_q_dmg = q_dmg * 0.75f;
		if (hp <= 0.f || hp > effective_q_dmg) return false;

		out_hp = hp;

		// Score: lower is better. HP urgency, minion-type bonus, small distance penalty.
		float score = hp;
		const float hp_percent = minion->get_max_hp() > 0.f ? hp / minion->get_max_hp() : 1.f;
		if      (hp_percent <= 0.1f) score -= 500.f;
		else if (hp_percent <= 0.2f) score -= 300.f;
		else if (hp_percent <= 0.3f) score -= 150.f;

		if      (cls == venplus::unit_class::lane_super) score -= 20000.f;
		else if (cls == venplus::unit_class::lane_siege) score -= 10000.f;

		score += dist * 0.1f;

		out_score = score;
		return true;
	}

	// before_attack — block AA when Q alone can lasthit the lane minion.
	bool __stdcall on_before_attack(orb_sdk::event_data* data)
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return true;
		if (!data || !data->target) return true;
		if (!sdk::orbwalker) return true;

		const bool in_lasthit = sdk::orbwalker->lasthit();
		const bool in_clear = sdk::orbwalker->clear();
		if (!in_lasthit && !in_clear) return true;

		if (in_lasthit && !sdk::orbwalker->is_spell_farm_lasthit_enabled()) return true;
		if (in_clear   && !sdk::orbwalker->is_spell_farm_enabled())         return true;

		// orb_sdk already filtered to hostile attackable; just verify alive + lane minion.
		auto* target = data->target;
		if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return true;
		if (!venplus::db::minions::is_lane_minion(venplus::classify_unit(target))) return true;

		if (in_lasthit && !menu::use_q_lasthit) return true;
		if (in_clear   && !menu::use_q_clear)   return true;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return true;

		auto q = player->get_spell(SLOT_Q);
		if (!q || q->get_level() == 0 || q->get_cooldown() > 0.f) return true;
		if (player->get_par() < player->get_spell_mana_cost(SLOT_Q)) return true;

		const float now = g_sdk->clock_facade->get_game_time();
		float hp, score;
		if (!is_minion_lasthittable(target, player, spell_data::q_range, now, hp, score))
			return true; // Q can't kill — allow AA

		// Long-range safety: if the minion is taking heavy incoming damage from other
		if (sdk::health_prediction && in_lasthit)
		{
			const float dist = player->get_position().distance(target->get_position());
			if (dist > spell_data::q_range * 0.75f)
			{
				try
				{
					const float land_time = venplus::math::travel_time(dist, spell_data::q_speed, spell_data::q_delay);
					const float hp_now = target->get_hp();
					const float hp_mid = sdk::health_prediction->get_predicted_health(target, now + land_time * 0.5f);
					// Minion is losing HP so fast from other sources it may die before Q lands
					if (hp_mid > 0.f && hp_mid < hp_now * 0.4f)
						return true;
				}
				catch (...) {}
			}
		}

		return false; // Block AA — Q will lasthit this minion
	}

	// True if Q's linear hitbox can clip both minions (with travel-time lead).
	static bool can_q_hit_both(game_object* m1, game_object* m2, game_object* player, float q_range, float now)
	{
		const auto ppos = player->get_position();
		auto m1pos = m1->get_position();
		auto m2pos = m2->get_position();

		const float dist1 = ppos.distance(m1pos);
		const float dist2 = ppos.distance(m2pos);
		if (dist1 > q_range || dist2 > q_range) return false;

		// Lead both positions by Q travel time.
		const float travel_time = ((dist1 + dist2) * 0.5f) / spell_data::q_speed;
		m1pos = m1pos + m1->get_velocity() * travel_time;
		m2pos = m2pos + m2->get_velocity() * travel_time;

		const auto dir1 = (m1pos - ppos).to_direction();
		const auto to_m2 = m2pos - ppos;
		const float proj_len = dir1.x * to_m2.x + dir1.z * to_m2.z;
		if (proj_len < 0.f || proj_len > dist1 + 50.f) return false;

		const auto proj_point = ppos + dir1 * proj_len;
		const float hit_radius = spell_data::q_radius
			+ std::max(venplus::unit_gameplay_radius(m1), venplus::unit_gameplay_radius(m2));
		return m2pos.distance(proj_point) <= hit_radius;
	}

	// Q skillshot pred_sdk::spell_data with standard collision filter.
	static pred_sdk::spell_data make_q_spell_data(game_object* player, int hitchance)
	{
		pred_sdk::spell_data s{};
		s.spell_type        = pred_sdk::spell_type::linear;
		s.targetting_type   = pred_sdk::targetting_type::center;
		s.spell_slot        = SLOT_Q;
		s.range             = spell_data::q_range;
		s.radius            = spell_data::q_radius;
		s.delay             = spell_data::q_delay;
		s.projectile_speed  = spell_data::q_speed;
		s.expected_hitchance = hitchance;

		s.forbidden_collisions = {
			pred_sdk::collision_type::hero,
			pred_sdk::collision_type::yasuo_wall,
			pred_sdk::collision_type::braum_wall,
		};
		const bool ignore_minion = menu::q_no_minion_collision_enabled
			&& player->get_level() >= menu::q_no_minion_collision_level;
		if (!ignore_minion)
			s.forbidden_collisions.push_back(pred_sdk::collision_type::unit);
		return s;
	}

	static bool pair_still_killable(game_object* m1, game_object* m2, float impact_time)
	{
		if (!sdk::health_prediction || !g_sdk->clock_facade) return true;

		const float q_dmg1 = get_q_damage(m1);
		const float q_dmg2 = get_q_damage(m2);

		float hp1 = m1->get_hp();
		float hp2 = m2->get_hp();
		const float pred1 = sdk::health_prediction->get_predicted_health(m1, impact_time);
		const float pred2 = sdk::health_prediction->get_predicted_health(m2, impact_time);
		if (pred1 > 0.f && pred1 <= m1->get_max_hp() && pred1 <= hp1) hp1 = pred1;
		if (pred2 > 0.f && pred2 <= m2->get_max_hp() && pred2 <= hp2) hp2 = pred2;

		// 25% buffer matches single-lasthit's effective_q_dmg = q_dmg * 0.75.
		return hp1 > 0.f && hp1 <= q_dmg1 * 0.75f
			&& hp2 > 0.f && hp2 <= q_dmg2 * 0.75f;
	}

	// Prefer cannon for the SDK predict source; otherwise pick the closer minion.
	static game_object* pick_double_primary(
		game_object* m1, game_object* m2, float dist1, float dist2)
	{
		const bool m1_siege = venplus::classify_unit(m1) == venplus::unit_class::lane_siege;
		const bool m2_siege = venplus::classify_unit(m2) == venplus::unit_class::lane_siege;
		if (m1_siege && !m2_siege) return m1;
		if (m2_siege && !m1_siege) return m2;
		return (dist1 < dist2) ? m1 : m2;
	}

	// Double-Q score adjustment: cannon bonuses + far-apart penalty.
	static float double_lasthit_bonus(
		game_object* m1, game_object* m2,
		const math::vector3& m1pos, const math::vector3& m2pos)
	{
		const bool m1_siege = venplus::classify_unit(m1) == venplus::unit_class::lane_siege;
		const bool m2_siege = venplus::classify_unit(m2) == venplus::unit_class::lane_siege;

		float bonus = 0.f;
		if (m1_siege && m2_siege)        bonus -= 10000.f; // double cannon
		else if (m1_siege || m2_siege)   bonus -=  5000.f; // mixed cannon + caster

		const float minion_dist = m1pos.distance(m2pos);
		if (minion_dist > 300.f) bonus += minion_dist * 0.5f; // alignment penalty
		return bonus;
	}

	void lasthit(bool allow_any_mode)
	{
		// Mode gating: Q in lasthit/clear, or forced via auto-Q hotkey path.
		bool is_lasthit_mode = false, is_clear_mode = false;
		if (sdk::orbwalker)
		{
			is_lasthit_mode = sdk::orbwalker->lasthit();
			is_clear_mode   = sdk::orbwalker->clear();
			if (!is_lasthit_mode && !is_clear_mode && !allow_any_mode) return;
			if (is_clear_mode   && !sdk::orbwalker->is_spell_farm_enabled())         return;
			if (is_lasthit_mode && !sdk::orbwalker->is_spell_farm_lasthit_enabled()) return;
		}
		else if (!allow_any_mode)
		{
			return;
		}
		if (is_lasthit_mode && !menu::use_q_lasthit) return;
		if ((is_clear_mode || allow_any_mode) && !menu::use_q_clear) return;

		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		auto q = player->get_spell(SLOT_Q);
		if (!q || q->get_level() == 0 || q->get_cooldown() > 0.f) return;
		if (player->get_par() < player->get_spell_mana_cost(SLOT_Q)) return;

		const float q_range = spell_data::q_range;
		const float now     = g_sdk->clock_facade->get_game_time();
		const int   player_team = player->get_team_id();
		const auto  ppos    = player->get_position();

		auto is_still_q_killable = [&](game_object* m) -> bool {
			if (!is_valid_minion_target(m, player_team)) return false;
			float hp_dummy = 0.f, score_dummy = 0.f;
			return is_minion_lasthittable(m, player, q_range,
				g_sdk->clock_facade->get_game_time(), hp_dummy, score_dummy);
		};

		auto cast_at = [&](const math::vector3& pos) {
			if (!sdk::common) player->cast_spell(SLOT_Q, pos);
			else              sdk::common->cast_spell(SLOT_Q, pos);
		};

		// Single walk over minions to build the lasthittable set, used by both
		struct candidate { game_object* m; float score; };
		std::vector<candidate> killable;
		for (auto minion : g_sdk->object_manager->get_minions())
		{
			float hp, score;
			if (is_minion_lasthittable(minion, player, q_range, now, hp, score))
				killable.push_back({ minion, score });
		}
		if (killable.empty()) return;

		// Double-Q: fire at a position that hits two lasthittable minions for 2 stacks.
		if (menu::use_q_double_lasthit && killable.size() >= 2)
		{
			struct best_pair { game_object* m1; game_object* m2; float score; math::vector3 cast_pos; };
			best_pair best{ nullptr, nullptr, FLT_MAX, {} };

			for (size_t i = 0; i < killable.size(); ++i)
			for (size_t j = i + 1; j < killable.size(); ++j)
			{
				auto m1 = killable[i].m;
				auto m2 = killable[j].m;
				if (!can_q_hit_both(m1, m2, player, q_range, now)) continue;

				const float dist1   = ppos.distance(m1->get_position());
				const float dist2   = ppos.distance(m2->get_position());
				const float travel  = ((dist1 + dist2) * 0.5f) / spell_data::q_speed;
				const float impact  = now + spell_data::q_delay + 0.1f + travel;

				if (!pair_still_killable(m1, m2, impact)) continue;

				// SDK predict on chosen primary; fallback aims at the farther minion.
				game_object* primary = pick_double_primary(m1, m2, dist1, dist2);
				math::vector3 cast_pos = primary->get_position();
				if (sdk::prediction)
				{
					auto spell = make_q_spell_data(player, pred_sdk::hitchance::medium);
					pred_sdk::pred_data pred{};
					try { pred = sdk::prediction->predict(primary, spell); }
					catch (...) { pred.is_valid = false; }
					if (pred.is_valid) cast_pos = pred.cast_position;
				}
				else
				{
					cast_pos = (dist1 > dist2 ? m1 : m2)->get_position();
				}

				// Verify both at impact lie within Q's hitbox along the cast line.
				const auto m1pos = m1->get_position() + m1->get_velocity() * travel;
				const auto m2pos = m2->get_position() + m2->get_velocity() * travel;
				const auto dir   = (cast_pos - ppos).to_direction();
				auto on_line = [&](const math::vector3& mpos, game_object* m) {
					const auto to_m = mpos - ppos;
					const float proj = dir.x * to_m.x + dir.z * to_m.z;
					if (proj < 0.f || proj > q_range) return false;
					const auto pp = ppos + dir * proj;
					return mpos.distance(pp) <= spell_data::q_radius
						+ venplus::unit_gameplay_radius(m);
				};
				if (!on_line(m1pos, m1) || !on_line(m2pos, m2)) continue;

				const float pair_score = killable[i].score + killable[j].score
					+ double_lasthit_bonus(m1, m2, m1pos, m2pos);
				if (pair_score < best.score)
					best = { m1, m2, pair_score, cast_pos };
			}

			// Recheck — if either minion died, fall through to single-lasthit.
			if (best.m1 && best.m2 && is_still_q_killable(best.m1) && is_still_q_killable(best.m2))
			{
				cast_at(best.cast_pos);
				return;
			}
		}

		// Single-Q on best-scored minion.
		auto best_it = std::min_element(killable.begin(), killable.end(),
			[](const candidate& a, const candidate& b) { return a.score < b.score; });
		auto* best = best_it->m;
		if (!is_still_q_killable(best)) return;

		if (sdk::prediction)
		{
			auto spell = make_q_spell_data(player, pred_sdk::hitchance::medium);
			pred_sdk::pred_data pred{};
			try { pred = sdk::prediction->predict(best, spell); }
			catch (...) { pred.is_valid = false; }
			if (pred.is_valid && is_still_q_killable(best))
			{
				cast_at(pred.cast_position);
				return;
			}
		}
		if (!is_still_q_killable(best)) return;
		cast_at(best->get_position());
	}

	void clear()
	{
		lasthit(); // Q single-target last-hit covers lane minion clear

		if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
		if (is_casting()) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		const float max_par = player->get_max_par();
		if (max_par <= 0.f) return;

		const float mana_pct = (player->get_par() / max_par) * 100.f;
		const auto  ppos = player->get_position();

		// Lane W AoE clear
		if (menu::use_w_clear && mana_pct >= static_cast<float>(menu::farm_mana_percent))
		{
			auto w = player->get_spell(SLOT_W);
			if (w && w->get_level() > 0 && w->get_cooldown() <= 0.f
				&& player->get_par() >= player->get_spell_mana_cost(SLOT_W))
			{
				// In clear: don't cancel autos for waves.
				if (!(sdk::orbwalker && sdk::orbwalker->would_cancel_attack()))
				{
					// Pick the seed minion whose W catches the most lane minions.
					const int player_team = player->get_team_id();
					game_object* best_seed = nullptr;
					int          best_hit = 0;

					for (auto seed : g_sdk->object_manager->get_minions())
					{
						if (!is_valid_minion_target(seed, player_team)) continue;
						if (!venplus::db::minions::is_lane_minion(venplus::classify_unit(seed))) continue;
						if (!seed->is_visible()) continue; // skip fog-of-war minions
						if (ppos.distance(seed->get_position()) > spell_data::w_range) continue;

						int hit = 0;
						for (auto other : g_sdk->object_manager->get_minions())
						{
							if (!is_valid_minion_target(other, player_team)) continue;
							if (!venplus::db::minions::is_lane_minion(venplus::classify_unit(other))) continue;
							if (!other->is_visible()) continue;
							if (venplus::math::point_in_circle(other->get_position(), seed->get_position(), spell_data::w_radius))
								++hit;
						}

						if (hit > best_hit) { best_hit = hit; best_seed = seed; }
					}

					if (best_seed && best_hit >= menu::w_clear_min_minions)
					{
						if (!sdk::common) player->cast_spell(SLOT_W, best_seed->get_position());
						else                sdk::common->cast_spell(SLOT_W, best_seed->get_position());
					}
				}
			}
		}

		// Jungle clear (Q/W)
		if (mana_pct < static_cast<float>(menu::jungle_mana_percent)) return;
		if (!menu::use_q_jungle && !menu::use_w_jungle) return;

		// Pick monster: epic > large > nearest. Skip fog-of-war.
		const int jungle_player_team = player->get_team_id();
		game_object* best_monster = nullptr;
		float best_score = FLT_MAX;
		for (auto m : g_sdk->object_manager->get_monsters())
		{
			if (!is_valid_minion_target(m, jungle_player_team)) continue;
			if (!m->is_visible()) continue;
			const auto cls = venplus::classify_unit(m);
			if (!venplus::db::minions::is_jungle(cls)) continue; // also excludes plants/wards
			const float d = ppos.distance(m->get_position());
			if (d > spell_data::q_range && d > spell_data::w_range) continue;

			float score = d;
			if (cls == venplus::unit_class::monster_epic)       score -= 300000.f;
			else if (cls == venplus::unit_class::monster_large) score -= 200000.f;
			else if (cls == venplus::unit_class::monster)       score -= 100000.f;

			if (score < best_score) { best_score = score; best_monster = m; }
		}

		if (!best_monster) return;

		// Q jungle — direct cast (monsters don't dodge).
		if (menu::use_q_jungle)
		{
			auto q = player->get_spell(SLOT_Q);
			if (q && q->get_level() > 0 && q->get_cooldown() <= 0.f
				&& player->get_par() >= player->get_spell_mana_cost(SLOT_Q))
			{
				if (ppos.distance(best_monster->get_position()) <= spell_data::q_range)
				{
					if (!sdk::common) player->cast_spell(SLOT_Q, best_monster->get_position());
					else              sdk::common->cast_spell(SLOT_Q, best_monster->get_position());
				}
			}
		}

		// W jungle — find tightest cluster around best monster.
		if (menu::use_w_jungle)
		{
			auto w = player->get_spell(SLOT_W);
			if (w && w->get_level() > 0 && w->get_cooldown() <= 0.f
				&& player->get_par() >= player->get_spell_mana_cost(SLOT_W))
			{
				if (ppos.distance(best_monster->get_position()) <= spell_data::w_range)
				{
					game_object* best_seed = best_monster;
					int          best_hit = 1;

					for (auto seed : g_sdk->object_manager->get_monsters())
					{
						if (!is_valid_minion_target(seed, jungle_player_team)) continue;
						if (!seed->is_visible()) continue;
						const auto seed_cls = venplus::classify_unit(seed);
						if (!venplus::db::minions::is_jungle(seed_cls)) continue;
						if (ppos.distance(seed->get_position()) > spell_data::w_range) continue;

						int hit = 0;
						for (auto other : g_sdk->object_manager->get_monsters())
						{
							if (!is_valid_minion_target(other, jungle_player_team)) continue;
							if (!other->is_visible()) continue;
							if (!venplus::db::minions::is_jungle(venplus::classify_unit(other))) continue;
							if (venplus::math::point_in_circle(other->get_position(), seed->get_position(), spell_data::w_radius))
								++hit;
						}
						// Prefer seeds that include epic/large monsters
						if (seed_cls == venplus::unit_class::monster_epic)       hit += 5;
						else if (seed_cls == venplus::unit_class::monster_large) hit += 3;

						if (hit > best_hit) { best_hit = hit; best_seed = seed; }
					}

					if (!sdk::common) player->cast_spell(SLOT_W, best_seed->get_position());
					else              sdk::common->cast_spell(SLOT_W, best_seed->get_position());
				}
			}
		}
	}

	void killsteal()
	{
		if (!g_sdk || !g_sdk->object_manager) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return;

		const float now = g_sdk->clock_facade->get_game_time();

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero)) continue;

			// Q/W are magic — include magical_shield (Banshee's etc.).
			const float magic_shields = hero->get_all_shield() + hero->get_magical_shield();

			if (menu::ks_q)
			{
				float hp = hero->get_hp() + magic_shields;
				if (sdk::health_prediction)
				{
					const float dist = player->get_position().distance(hero->get_position());
					const float t = now + venplus::math::travel_time(dist, spell_data::q_speed, spell_data::q_delay);
					const float pred = sdk::health_prediction->get_predicted_health(hero, t);
					if (pred > 0.f && pred <= hero->get_max_hp())
						hp = pred + magic_shields;
				}
				if (hp > 0.f && hp <= get_q_damage(hero))
					if (cast_q(hero)) return;
			}

			if (menu::ks_w)
			{
				float hp = hero->get_hp() + magic_shields;
				if (sdk::health_prediction)
				{
					const float pred = sdk::health_prediction->get_predicted_health(hero, now + spell_data::w_delay);
					if (pred > 0.f && pred <= hero->get_max_hp())
						hp = pred + magic_shields;
				}
				if (hp > 0.f && hp <= get_w_damage(hero))
					if (cast_w(hero)) return;
			}

			if (menu::ks_r)
			{
				// Spell shield → skip. Anti-burst is gated inside cast_r.
				if (hero->has_buff_of_type(buff_type::spell_shield)) continue;
				if (can_r_kill_target(hero) && cast_r(hero)) return;
			}
		}
	}

	// Anti-gapclose: place E border between Veigar and an approaching enemy.
	void anti_gap_e()
	{
		if (!menu::use_e_antigap) return;
		if (!g_sdk || !g_sdk->object_manager) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_E)) return;
			if (!sdk::common->spell->is_spell_ready(SLOT_E))   return;
		}
		else
		{
			auto e_chk = player->get_spell(SLOT_E);
			if (!e_chk || e_chk->get_level() == 0 || e_chk->get_cooldown() > 0.f) return;
		}

		if (player->get_par() < player->get_spell_mana_cost(SLOT_E)) return;

		const auto  ppos = player->get_position();
		const float cast_range = spell_data::e_range;

		game_object* best_target = nullptr;
		float        best_threat = 0.f;
		math::vector3 best_cast_pos{};

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero)) continue;

			if (!menu::e_whitelist.empty())
			{
				const auto it = menu::e_whitelist.find(hero->get_char_name());
				if (it != menu::e_whitelist.end() && !it->second) continue;
			}

			const auto  epos = hero->get_position();
			const float dist = ppos.distance(epos);
			if (dist > cast_range + spell_data::e_outer_radius) continue;

			// Approach speed: enemy velocity projected onto vector toward player.
			const auto  vel = hero->get_velocity();
			const float inv_dist = 1.f / (dist + 0.001f);
			const float approach_sp = (vel.x * (ppos.x - epos.x) + vel.z * (ppos.z - epos.z)) * inv_dist;
			const bool  is_dash = hero->is_dashing();
			if (!is_dash && approach_sp < menu::ANTIGAP_MIN_SPEED) continue;

			// Border sits between us and them: center = player + dir * (dist - outer).
			const float center_dist = std::clamp(dist - spell_data::e_outer_radius, 50.f, cast_range);
			const auto  dir = (epos - ppos).to_direction();
			math::vector3 cast_pos = ppos + dir * center_dist;
			if (ppos.distance(cast_pos) > cast_range)
				cast_pos = ppos + (cast_pos - ppos).to_direction() * cast_range;

			const float threat = is_dash ? approach_sp + 1000.f : approach_sp;
			if (threat > best_threat)
			{
				best_threat = threat;
				best_target = hero;
				best_cast_pos = cast_pos;
			}
		}

		if (!best_target) return;

		anti_gap_fired = true;

		if (!sdk::common) player->cast_spell(SLOT_E, best_cast_pos);
		else                sdk::common->cast_spell(SLOT_E, best_cast_pos);
	}

	void auto_w_on_cc()
	{
		if (!menu::auto_w_on_cc) return;
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		const float now = g_sdk->clock_facade->get_game_time();

		// Don't double-cast — W already in flight
		if (w_track.active && now < w_track.impact_time + 0.1f) return;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_W)) return;
			if (!sdk::common->spell->is_spell_ready(SLOT_W))   return;
		}
		else
		{
			auto w_chk = player->get_spell(SLOT_W);
			if (!w_chk || w_chk->get_level() == 0 || w_chk->get_cooldown() > 0.f) return;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_W)) return;

		static constexpr uint32_t VEIGAR_E_STUN_HASH = 0x37D29774u;
		const auto ppos = player->get_position();

		// Two priority tiers — own-E stun beats general CC; closest within tier wins.
		// In-cage targets fold into the general tier (cage already guarantees the W).
		struct candidate { game_object* hero; float distance; bool direct_cast; };
		candidate own_best{ nullptr, FLT_MAX, true };
		candidate gen_best{ nullptr, FLT_MAX, false };

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero, spell_data::w_range)) continue;
			if (!hero->is_visible()) continue;

			const float cc_remaining = get_cc_remaining(hero);
			if (cc_remaining <= 0.f) continue;

			auto* own_stun = hero->get_buff_by_hash(VEIGAR_E_STUN_HASH);
			const bool has_own_e_stun = own_stun && own_stun->is_active();
			const bool in_cage = is_in_our_cage(hero, now);
			const float d = ppos.distance(hero->get_position());

			if (has_own_e_stun)
			{
				if (d < own_best.distance) own_best = { hero, d, true };
			}
			else if (in_cage || cc_remaining >= spell_data::w_delay)
			{
				// in_cage → direct cast (target trapped); else CC needs prediction.
				if (d < gen_best.distance) gen_best = { hero, d, in_cage };
			}
		}

		const candidate& best = own_best.hero ? own_best : gen_best;
		if (!best.hero) return;

		if (best.direct_cast)
		{
			// Target is locked (own-E stun or trapped in cage) — fire at current pos.
			const auto tpos = best.hero->get_position();
			if (sdk::common) sdk::common->cast_spell(SLOT_W, tpos);
			else             player->cast_spell(SLOT_W, tpos);
			m_last_w_cast_time = now;
		}
		else
		{
			cast_w(best.hero);
		}
	}

	// W on any enemy inside our active E cage — they can't leave, W is guaranteed.
	void auto_w_in_cage()
	{
		if (!menu::auto_w_in_cage) return;
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
		if (!e_wall_track.active) return;

		const float now = g_sdk->clock_facade->get_game_time();
		if (now >= e_wall_track.end_time) { e_wall_track.active = false; return; }

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;
		if (w_track.active && now < w_track.impact_time + 0.1f) return;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_W)) return;
			if (!sdk::common->spell->is_spell_ready(SLOT_W))   return;
		}
		else
		{
			auto w_chk = player->get_spell(SLOT_W);
			if (!w_chk || w_chk->get_level() == 0 || w_chk->get_cooldown() > 0.f) return;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_W)) return;
		if (now - m_last_w_cast_time < MIN_SPELL_DELAY) return;

		const auto ppos = player->get_position();
		game_object* best_target = nullptr;
		float        best_dist = FLT_MAX;

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero, spell_data::w_range)) continue;
			if (!hero->is_visible()) continue;
			if (!is_in_our_cage(hero, now)) continue;
			const float d = ppos.distance(hero->get_position());
			if (d < best_dist) { best_dist = d; best_target = hero; }
		}
		if (!best_target) return;

		// Direct cast — target is trapped in the cage.
		const auto tpos = best_target->get_position();
		if (sdk::common) sdk::common->cast_spell(SLOT_W, tpos);
		else             player->cast_spell(SLOT_W, tpos);
		m_last_w_cast_time = now;
	}

	void auto_e_on_stun()
	{
		if (!menu::auto_e_on_stun) return;
		if (!g_sdk || !g_sdk->object_manager) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_E)) return;
			if (!sdk::common->spell->is_spell_ready(SLOT_E))   return;
		}
		else
		{
			auto e_chk = player->get_spell(SLOT_E);
			if (!e_chk || e_chk->get_level() == 0 || e_chk->get_cooldown() > 0.f) return;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_E)) return;

		const auto  ppos = player->get_position();
		const float e_reach = spell_data::e_range + spell_data::e_outer_radius;
		game_object* best_target = nullptr;
		float        best_priority = 0.f;

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero, e_reach)) continue;
			if (!menu::e_whitelist.empty())
			{
				const auto it = menu::e_whitelist.find(hero->get_char_name());
				if (it != menu::e_whitelist.end() && !it->second) continue;
			}

			// Knockup/suppression also can't turn away from the cage edge.
			const bool is_hard_cc =
				hero->has_buff_of_type(buff_type::stun)      ||
				hero->has_buff_of_type(buff_type::knockup)   ||
				hero->has_buff_of_type(buff_type::knockback) ||
				hero->has_buff_of_type(buff_type::suppression);
			if (!is_hard_cc) continue;

			// Higher HP, closer = more valuable.
			const float priority = hero->get_hp() / (ppos.distance(hero->get_position()) + 1.f);
			if (priority > best_priority) { best_priority = priority; best_target = hero; }
		}
		if (!best_target) return;

		cast_e_edge_stun(best_target, true);
	}

	// Cast E on enemies exiting stasis (Zhonya's, GA, Bard R) — guaranteed hit.
	void stasis_e()
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
		if (!sdk::common || !sdk::common->buffs) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		try
		{
			auto e_spell = player->get_spell(SLOT_E);
			if (!e_spell || e_spell->get_level() == 0 || e_spell->get_cooldown() > 0.f) return;
			if (player->get_par() < player->get_spell_mana_cost(SLOT_E)) return;

			const float now = g_sdk->clock_facade->get_game_time();
			if (now - last_cast_e < spell_data::e_delay) return;

			const auto  ppos = player->get_position();
			const float e_reach = spell_data::e_range + spell_data::e_outer_radius;

			for (auto hero : g_sdk->object_manager->get_heroes())
			{
				if (!hero || !hero->is_valid() || hero->is_dead()) continue;
				if (hero->get_team_id() == player->get_team_id()) continue;

				const auto  hpos = hero->get_position();
				const float dist = ppos.distance(hpos);
				if (dist > e_reach) continue;

				if (!menu::e_whitelist.empty())
				{
					const auto it = menu::e_whitelist.find(hero->get_char_name());
					if (it != menu::e_whitelist.end() && !it->second) continue;
				}

				try
				{
					auto stasis = sdk::common->buffs->get_stasis_buff(hero);
					if (!stasis || !stasis->is_active()) continue;

					const float remaining = stasis->get_end_time() - now;
					if (remaining <= 0.f) continue;

					// Fire so E lands as stasis ends — e_delay=0.75s, accept ±tolerance.
					if (remaining >= spell_data::e_delay - 0.05f
						&& remaining <= spell_data::e_delay + 0.15f)
					{
						if (!sdk::common) { player->cast_spell(SLOT_E, hpos); last_cast_e = now; return; }
						if (sdk::common->cast_spell(SLOT_E, hpos)) { last_cast_e = now; return; }
					}
				}
				catch (...) { continue; }
			}
		}
		catch (...) { /* Silently fail if stasis detection crashes */ }
	}

	static bool is_position_pathable(const math::vector3& pos)
	{
		if (!g_sdk || !g_sdk->nav_mesh) return true; // Assume pathable if nav_mesh unavailable

		math::vector3 check_pos = pos;
		return g_sdk->nav_mesh->is_pathable(check_pos);
	}

	// Velocity-based fallback prediction with movement-direction lead.
	static math::vector3 predict_position_velocity(game_object* target, float delay, const math::vector3& player_pos)
	{
		if (!target) return {};
		if (!target->is_valid()) return target->get_position();

		const auto tpos = target->get_position();
		const auto tvel = target->get_velocity();
		const float speed_sq = tvel.x * tvel.x + tvel.z * tvel.z;
		const float speed = std::sqrt(speed_sq);

		if (speed < prediction_constants::stationary_speed_threshold) return tpos;

		// dot > 0: target running AWAY from player; dot < 0: running TOWARD.
		const auto dir_to_target = (tpos - player_pos).to_direction();
		const auto vel_dir = (speed > 0.001f) ? math::vector3{ tvel.x / speed, 0.f, tvel.z / speed } : math::vector3{};
		const float dot = dir_to_target.x * vel_dir.x + dir_to_target.z * vel_dir.z;

		const bool is_dashing = target->is_dashing();
		const bool is_fast = speed > prediction_constants::fast_movement_threshold;
		const float threshold = prediction_constants::movement_direction_threshold;
		const bool running_away         = dot >  threshold;
		const bool running_toward       = dot < -threshold;
		const bool moving_perpendicular = std::abs(dot) < threshold;

		float prediction_mult = 1.0f;
		if      (is_dashing)                          prediction_mult = 1.0f;
		else if (running_away)                        prediction_mult = 1.2f;  // more lead
		else if (moving_perpendicular && is_fast)     prediction_mult = 1.15f;
		else if (running_toward)                      prediction_mult = 0.9f;  // less lead

		return tpos + tvel * (delay * prediction_mult);
	}

	// W landing prediction: SDK first, then dash-path, then velocity-direction lead.
	static math::vector3 predict_w_landing(
		game_object* target,
		const math::vector3& ppos,
		const math::vector3& dir_to_target,
		const math::vector3& tvel,
		float tvel_mag)
	{
		const auto tpos = target->get_position();

		if (sdk::prediction)
		{
			pred_sdk::spell_data spell{};
			spell.spell_type        = pred_sdk::spell_type::circular;
			spell.targetting_type   = pred_sdk::targetting_type::center;
			spell.spell_slot        = SLOT_W;
			spell.range             = spell_data::w_range;
			spell.radius            = spell_data::w_radius;
			spell.delay             = spell_data::w_delay;
			spell.projectile_speed  = FLT_MAX;
			spell.expected_hitchance = menu::w_hitchance;

			pred_sdk::pred_data pred{};
			try { pred = sdk::prediction->predict(target, spell); }
			catch (...) { pred.is_valid = false; }

			if (pred.is_valid && pred.hitchance >= menu::w_hitchance)
			{
				// Sanity-check range (tolerate small SDK rounding)
				if (ppos.distance(pred.cast_position) <= spell_data::w_range + 50.f)
					return pred.cast_position;
			}
		}

		if (target->is_dashing())
		{
			auto path = target->get_path();
			if (!path.empty())
			{
				const auto dash_end = path.back();
				const float dash_dist = tpos.distance(dash_end);
				const float dash_speed = target->get_dash_speed();
				if (dash_speed > 0.01f && dash_dist > 0.01f)
				{
					const float dash_time = dash_dist / dash_speed;
					const auto dash_dir   = (dash_end - tpos).to_direction();
					if (dash_time < spell_data::w_delay)
					{
						// Dash completes before W lands; predict post-dash drift
						const float remaining = spell_data::w_delay - dash_time;
						const float post_speed = std::max(tvel_mag, 300.f);
						return dash_end + dash_dir * (post_speed * remaining);
					}
					return tpos + dash_dir * (dash_speed * spell_data::w_delay);
				}
			}
		}

		if (tvel_mag <= 50.f) return tpos;

		const float dot = dir_to_target.x * (tvel.x / tvel_mag) + dir_to_target.z * (tvel.z / tvel_mag);
		float lead = 1.f;
		if      (dot < -0.3f) lead = 1.25f; // moving away — more lead
		else if (dot >  0.3f) lead = 0.95f; // moving toward — less lead
		else                  lead = 1.15f; // perpendicular
		if (tvel_mag > 400.f) lead += (tvel_mag - 400.f) * 0.0005f;

		return tpos + tvel * (spell_data::w_delay * lead);
	}

	// Nudge cast_pos toward player's velocity direction to compensate for
	// player repositioning during W's 1.47s formation.
	static math::vector3 apply_player_movement_offset(
		const math::vector3& cast_pos,
		const math::vector3& pvel,
		float pvel_mag,
		bool is_player_fleeing)
	{
		if (pvel_mag <= 100.f || is_player_fleeing) return cast_pos;
		const auto pvel_dir = math::vector3{ pvel.x / pvel_mag, 0.f, pvel.z / pvel_mag };
		const float adjustment = std::min(60.f, pvel_mag * spell_data::w_delay * 0.15f);
		return cast_pos + pvel_dir * adjustment;
	}

	// Shared E predict struct for single-target and AOE candidate search.
	static pred_sdk::spell_data make_e_spell_data()
	{
		pred_sdk::spell_data s{};
		s.spell_type        = pred_sdk::spell_type::circular;
		s.targetting_type   = pred_sdk::targetting_type::center;
		s.spell_slot        = SLOT_E;
		s.range             = spell_data::e_range;
		s.radius            = spell_data::e_outer_radius;
		s.delay             = spell_data::e_delay;
		s.projectile_speed  = FLT_MAX;
		s.expected_hitchance = menu::e_hitchance;
		return s;
	}

	// Single-target E landing: stationary → current pos, moving → SDK predict
	// with velocity fallback on SDK miss / sub-menu hitchance.
	static math::vector3 predict_e_landing(game_object* target, const math::vector3& ppos)
	{
		const auto tpos = target->get_position();
		if (!target->is_moving()) return tpos;

		if (sdk::prediction)
		{
			auto spell = make_e_spell_data();
			pred_sdk::pred_data pred{};
			try { pred = sdk::prediction->predict(target, spell); }
			catch (...) { pred.is_valid = false; }
			if (pred.is_valid && pred.hitchance >= spell.expected_hitchance)
				return pred.cast_position;
		}
		return predict_position_velocity(target, spell_data::e_delay, ppos);
	}

	// Sub-550u: shift the cage so the stun wall lands on the target instead
	// of putting them in the inner safe zone. Clamps to cast range.
	static math::vector3 apply_e_distance_placement(
		const math::vector3& cast_pos, const math::vector3& ppos, float target_dist)
	{
		if (target_dist >= 550.f) return cast_pos;
		const auto dir = (cast_pos - ppos).to_direction();
		const math::vector3 wc = cast_pos - dir * spell_data::e_outer_radius;
		return (ppos.distance(wc) <= spell_data::e_range)
			? wc
			: ppos + dir * spell_data::e_range;
	}

	// Multi-target E placement: scores primary-predicted / centroid / per-enemy
	// candidates by stun-ring hits, returns best when ≥2 enemies hit.
	static bool find_e_aoe_position(
		game_object* target, const math::vector3& ppos, const math::vector3& tpos,
		math::vector3& out_pos)
	{
		if (!sdk::prediction) return false;

		const float max_range = spell_data::e_range + spell_data::e_outer_radius;
		std::vector<game_object*> enemies;
		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero, max_range)) continue;
			if (!menu::e_whitelist.empty())
			{
				const auto it = menu::e_whitelist.find(hero->get_char_name());
				if (it != menu::e_whitelist.end() && !it->second) continue;
			}
			enemies.push_back(hero);
		}
		if (enemies.size() < 2) return false;

		auto spell = make_e_spell_data();

		// Cache predicted positions per enemy
		struct enemy_pred { game_object* obj; math::vector3 pos; };
		std::vector<enemy_pred> cache;
		cache.reserve(enemies.size());
		for (auto enemy : enemies)
		{
			if (!enemy || !enemy->is_valid()) continue;
			math::vector3 pos;
			if (is_target_immobile(enemy)) { pos = enemy->get_position(); }
			else
			{
				pred_sdk::pred_data pred{};
				try { pred = sdk::prediction->predict(enemy, spell); }
				catch (...) { pred.is_valid = false; }
				pos = (pred.is_valid && pred.hitchance >= spell.expected_hitchance)
					? pred.cast_position
					: predict_position_velocity(enemy, spell_data::e_delay, ppos);
			}
			cache.push_back({ enemy, pos });
		}
		if (cache.size() < 2) return false;

		// Build candidate positions (primary predicted + centroid + per-enemy).
		std::vector<math::vector3> candidates;
		candidates.reserve(8);

		pred_sdk::pred_data primary_pred{};
		try { primary_pred = sdk::prediction->predict(target, spell); }
		catch (...) { primary_pred.is_valid = false; }
		if (primary_pred.is_valid && primary_pred.hitchance >= spell.expected_hitchance
			&& ppos.distance(primary_pred.cast_position) <= spell_data::e_range)
			candidates.push_back(primary_pred.cast_position);

		math::vector3 centroid{};
		for (const auto& e : cache) centroid = centroid + e.pos;
		const float inv_n = 1.f / static_cast<float>(cache.size());
		centroid.x *= inv_n; centroid.y *= inv_n; centroid.z *= inv_n;
		if (ppos.distance(centroid) <= spell_data::e_range)
			candidates.push_back(centroid);

		for (const auto& e : cache)
		{
			if (candidates.size() >= 8) break;
			if (ppos.distance(e.pos) <= spell_data::e_range)
				candidates.push_back(e.pos);
		}

		// Score each candidate by enemies inside the stun ring [inner-30, outer+30].
		constexpr float STUN_RING_MARGIN = 30.f;
		int best_hits = 0;
		bool best_hits_primary = false;
		for (const auto& cand : candidates)
		{
			math::vector3 clamped = (ppos.distance(cand) > spell_data::e_range)
				? ppos + (cand - ppos).to_direction() * spell_data::e_range
				: cand;
			if (!is_position_pathable(clamped)) continue;

			int hits = 0;
			bool hits_primary = false;
			for (const auto& e : cache)
			{
				if (!e.obj || !e.obj->is_valid()) continue;
				const float d = clamped.distance(e.pos);
				const float br = e.obj->get_bounding_radius();
				const float ring_min = spell_data::e_inner_radius - STUN_RING_MARGIN - br;
				const float ring_max = spell_data::e_outer_radius + STUN_RING_MARGIN + br;
				if (d >= ring_min && d <= ring_max)
				{
					hits++;
					if (e.obj == target) hits_primary = true;
				}
			}

			if (hits >= static_cast<int>(cache.size()) && hits_primary)
			{
				out_pos = clamped;
				return true; // perfect — early out
			}
			if (hits > best_hits || (hits == best_hits && hits_primary && !best_hits_primary))
			{
				best_hits = hits;
				best_hits_primary = hits_primary;
				out_pos = clamped;
			}
		}

		return best_hits >= 2;
	}

	// Velocity-based Q intercept fallback. Used for close range (skips hitchance
	static bool predict_q_landing_fallback(
		game_object* target, const math::vector3& ppos, const math::vector3& tpos,
		float dist, bool close_range, math::vector3& out_pos)
	{
		out_pos = tpos;
		if (target->is_dashing()) return true; // accept current pos for dashers

		const auto tvel = target->get_velocity();
		const float tgt_speed = std::sqrt(tvel.x * tvel.x + tvel.z * tvel.z);
		if (tgt_speed <= prediction_constants::stationary_speed_threshold) return true;

		const auto dir_to_tgt = (tpos - ppos).to_direction();
		const auto vel_dir = math::vector3{ tvel.x / tgt_speed, 0.f, tvel.z / tgt_speed };
		const float dot = dir_to_tgt.x * vel_dir.x + dir_to_tgt.z * vel_dir.z;
		const bool running_away   = dot >  prediction_constants::movement_direction_threshold;
		const bool running_toward = dot < -prediction_constants::movement_direction_threshold;

		// Close-range targets bypass — at <600u Q is reliable regardless.
		if (!close_range)
		{
			int approx_hc;
			if (running_away || running_toward) approx_hc = 85;
			else if (std::abs(dot) > 0.25f)     approx_hc = 55;
			else                                approx_hc = 30;
			if (approx_hc < menu::q_hitchance) return false;
		}

		const float travel_time = dist / spell_data::q_speed;
		const float away_comp   = running_away ? 0.09f : 0.f;
		out_pos = tpos + tvel * (spell_data::q_delay + travel_time + away_comp);

		return ppos.distance(out_pos) <= spell_data::q_range;
	}

	// "Remove Minion Collision at Level
	static bool q_blocked_by_minion(
		game_object* player, const math::vector3& ppos, const math::vector3& cast_pos)
	{
		if (!menu::q_no_minion_collision_enabled) return false;
		if (player->get_level() >= menu::q_no_minion_collision_level) return false;

		const auto  shot_dir = (cast_pos - ppos).to_direction();
		const float shot_len = ppos.distance(cast_pos);
		const int   player_team = player->get_team_id();

		for (auto obj : g_sdk->object_manager->get_minions())
		{
			if (!is_valid_minion_target(obj, player_team)) continue;
			if (!venplus::db::minions::is_lane_minion(venplus::classify_unit(obj))) continue;

			const auto  mpos = obj->get_position();
			const auto  to_m = mpos - ppos;
			const float proj = shot_dir.x * to_m.x + shot_dir.z * to_m.z;
			if (proj < 0.f || proj > shot_len) continue;

			const float perp_sq = (to_m.x * to_m.x + to_m.z * to_m.z) - proj * proj;
			const float hit_r = spell_data::q_radius + venplus::unit_gameplay_radius(obj);
			if (perp_sq < hit_r * hit_r) return true;
		}
		return false;
	}

	bool cast_q(game_object* target)
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

		const float now = g_sdk->clock_facade->get_game_time();
		if (now - m_last_q_cast_time < MIN_SPELL_DELAY) return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !target) return false;
		if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return false;
		if (target->has_buff_of_type(buff_type::spell_shield)) return false;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_Q)) return false;
			if (!sdk::common->spell->is_spell_ready(SLOT_Q))   return false;
		}
		else
		{
			auto q_chk = player->get_spell(SLOT_Q);
			if (!q_chk || q_chk->get_level() == 0 || q_chk->get_cooldown() > 0.f) return false;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_Q)) return false;

		const auto  ppos = player->get_position();
		const auto  tpos = target->get_position();
		const float dist = ppos.distance(tpos);
		if (dist > spell_data::q_range + target->get_bounding_radius()) return false;

		if (sdk::orbwalker && !sdk::orbwalker->combo() && !sdk::orbwalker->harass()
			&& sdk::orbwalker->would_cancel_attack()) return false;

		const bool  close_range = (dist <= 600.f);
		math::vector3 cast_pos = tpos;
		bool sdk_used = false;

		// SDK prediction
		if (sdk::prediction && !close_range)
		{
			pred_sdk::spell_data q{};
			q.spell_type        = pred_sdk::spell_type::linear;
			q.targetting_type   = pred_sdk::targetting_type::edge_to_edge;
			q.spell_slot        = SLOT_Q;
			q.range             = spell_data::q_range;
			q.radius            = spell_data::q_radius;
			q.delay             = spell_data::q_delay;
			q.projectile_speed  = spell_data::q_speed;
			q.expected_hitchance = menu::q_hitchance;

			pred_sdk::pred_data res{};
			try { res = sdk::prediction->predict(target, q); }
			catch (...) { res.is_valid = false; }

			if (res.is_valid && res.hitchance >= menu::q_hitchance)
			{
				cast_pos = res.cast_position;
				sdk_used = true;
			}
		}

		if (!sdk_used && !predict_q_landing_fallback(target, ppos, tpos, dist, close_range, cast_pos))
			return false;

		if (q_blocked_by_minion(player, ppos, cast_pos)) return false;

		if (!target->is_valid() || target->is_dead()) return false;

		bool result;
		if (!sdk::common) { player->cast_spell(SLOT_Q, cast_pos); result = true; }
		else              { result = sdk::common->cast_spell(SLOT_Q, cast_pos); }
		if (result) m_last_q_cast_time = now;
		return result;
	}

	bool cast_w(game_object* target, bool force)
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

		const float now = g_sdk->clock_facade->get_game_time();
		if (now - m_last_w_cast_time < MIN_SPELL_DELAY) return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !target) return false;
		if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return false;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_W)) return false;
			if (!sdk::common->spell->is_spell_ready(SLOT_W))   return false;
		}
		else
		{
			auto w_chk = player->get_spell(SLOT_W);
			if (!w_chk || w_chk->get_level() == 0 || w_chk->get_cooldown() > 0.f) return false;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_W)) return false;

		const auto  ppos = player->get_position();
		const auto  tpos = target->get_position();
		if (ppos.distance(tpos) > spell_data::w_range) return false;

		if (!force && sdk::orbwalker && !sdk::orbwalker->combo() && !sdk::orbwalker->harass()
			&& sdk::orbwalker->would_cancel_attack()) return false;

		const auto pvel = player->get_velocity();
		const auto tvel = target->get_velocity();
		const auto dir_to_target = (tpos - ppos).to_direction();
		const auto dir_to_player = (ppos - tpos).to_direction();
		const float pvel_mag = std::sqrt(pvel.x * pvel.x + pvel.z * pvel.z);
		const float tvel_mag = std::sqrt(tvel.x * tvel.x + tvel.z * tvel.z);
		const float pvel_dir_dot = (pvel_mag > 0.001f)
			? (dir_to_player.x * (pvel.x / pvel_mag) + dir_to_player.z * (pvel.z / pvel_mag)) : 0.f;
		const float tvel_dir_dot = (tvel_mag > 0.001f)
			? (dir_to_target.x * (tvel.x / tvel_mag) + dir_to_target.z * (tvel.z / tvel_mag)) : 0.f;

		const bool is_player_fleeing = pvel_dir_dot > 0.3f && pvel_mag > 100.f;
		const bool is_enemy_chasing  = tvel_dir_dot > 0.3f && tvel_mag > 100.f;
		const bool is_enemy_dashing  = target->is_dashing();
		const bool is_immobile       = is_target_immobile(target);
		const bool use_anti_gapclose = (is_player_fleeing || (sdk::orbwalker && sdk::orbwalker->flee()))
			&& (is_enemy_chasing || is_enemy_dashing);

		math::vector3 cast_pos;

		if (is_immobile)
		{
			// Verify CC outlasts W's 1.47s formation (0.1s slack). force bypasses.
			if (!force && get_cc_remaining(target) < spell_data::w_delay - 0.1f)
				return false;
			cast_pos = tpos;
		}
		else if (use_anti_gapclose)
		{
			// W between us — closer to player for dashes (40%), midway walking (55%).
			const auto pred_enemy = predict_w_landing(target, ppos, dir_to_target, tvel, tvel_mag);
			const auto dir_to_pred = (pred_enemy - ppos).to_direction();
			const float pred_dist  = ppos.distance(pred_enemy);

			float ratio = is_enemy_dashing ? 0.4f : 0.55f;
			float dist  = std::min(spell_data::w_range, pred_dist * ratio);
			if (dist < 150.f && pred_dist > 150.f) dist = 150.f; // min stand-off
			cast_pos = ppos + dir_to_pred * dist;
		}
		else
		{
			cast_pos = predict_w_landing(target, ppos, dir_to_target, tvel, tvel_mag);
			cast_pos = apply_player_movement_offset(cast_pos, pvel, pvel_mag, is_player_fleeing);
		}

		if (ppos.distance(cast_pos) > spell_data::w_range)
			cast_pos = ppos + (cast_pos - ppos).to_direction() * spell_data::w_range;

		if (!is_position_pathable(cast_pos) && g_sdk->nav_mesh)
		{
			math::vector3 nearest = g_sdk->nav_mesh->get_nearest_passable_position(cast_pos);
			if (ppos.distance(nearest) <= spell_data::w_range)
				cast_pos = nearest;
			else
				return false;
		}

		if (!target->is_valid() || target->is_dead()) return false;

		bool result;
		if (!sdk::common) { player->cast_spell(SLOT_W, cast_pos); result = true; }
		else              { result = sdk::common->cast_spell(SLOT_W, cast_pos); }
		if (result) m_last_w_cast_time = now;
		return result;
	}

	bool cast_e(game_object* target, bool anti_gapclose, bool force, bool wall_on_target)
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

		const float now = g_sdk->clock_facade->get_game_time();
		if (now - m_last_e_cast_time < MIN_SPELL_DELAY) return false;
		// Anti-stutter: per-tick prediction shifts the cast point slightly,
		if (now - last_cast_e < 0.25f) return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !target) return false;
		if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return false;

		if (!menu::e_whitelist.empty())
		{
			const auto it = menu::e_whitelist.find(target->get_char_name());
			if (it != menu::e_whitelist.end() && !it->second) return false;
		}

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_E)) return false;
			if (!sdk::common->spell->is_spell_ready(SLOT_E))   return false;
		}
		else
		{
			auto e_chk = player->get_spell(SLOT_E);
			if (!e_chk || e_chk->get_level() == 0 || e_chk->get_cooldown() > 0.f) return false;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_E)) return false;

		const auto  ppos = player->get_position();
		const auto  tpos = target->get_position();
		const float dist = ppos.distance(tpos);
		if (dist > spell_data::e_range + spell_data::e_outer_radius) return false;

		if (!force && sdk::orbwalker && !sdk::orbwalker->combo() && !sdk::orbwalker->harass()
			&& sdk::orbwalker->would_cancel_attack()) return false;

		math::vector3 cast_pos;

		if (anti_gapclose)
		{
			// Border between us: center = player + dir * (dist - outer_radius).
			const float center_dist = std::clamp(dist - spell_data::e_outer_radius,
				50.f, spell_data::e_range);
			cast_pos = ppos + (tpos - ppos).to_direction() * center_dist;
		}
		else if (is_target_immobile(target))
		{
			cast_pos = tpos;
		}
		else if (menu::e_prioritize_aoe && find_e_aoe_position(target, ppos, tpos, cast_pos))
		{
			// AOE picked — pathability already checked inside helper.
		}
		else
		{
			cast_pos = predict_e_landing(target, ppos);
			if (wall_on_target)
				cast_pos = apply_e_distance_placement(cast_pos, ppos, dist);
		}

		if (ppos.distance(cast_pos) > spell_data::e_range)
			cast_pos = ppos + (cast_pos - ppos).to_direction() * spell_data::e_range;

		if (!is_position_pathable(cast_pos) && g_sdk->nav_mesh)
		{
			math::vector3 nearest = g_sdk->nav_mesh->get_nearest_passable_position(cast_pos);
			if (ppos.distance(nearest) <= spell_data::e_range) cast_pos = nearest;
			else return false;
		}

		// Mark attempt unconditionally — anti-stutter gates failed attempts too.
		last_cast_e = now;
		bool result;
		if (!sdk::common) { player->cast_spell(SLOT_E, cast_pos); result = true; }
		else              { result = sdk::common->cast_spell(SLOT_E, cast_pos); }
		if (result) m_last_e_cast_time = now;
		return result;
	}

	bool cast_e_edge_stun(game_object* target, bool force)
	{
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

		const float now = g_sdk->clock_facade->get_game_time();
		if (now - m_last_e_cast_time < MIN_SPELL_DELAY) return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !target) return false;
		if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return false;

		if (!menu::e_whitelist.empty())
		{
			const auto it = menu::e_whitelist.find(target->get_char_name());
			if (it != menu::e_whitelist.end() && !it->second) return false;
		}

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_E)) return false;
			if (!sdk::common->spell->is_spell_ready(SLOT_E))   return false;
		}
		else
		{
			auto e_chk = player->get_spell(SLOT_E);
			if (!e_chk || e_chk->get_level() == 0 || e_chk->get_cooldown() > 0.f) return false;
		}
		if (player->get_par() < player->get_spell_mana_cost(SLOT_E)) return false;

		const auto  ppos = player->get_position();
		const auto  tpos = target->get_position();
		if (ppos.distance(tpos) > spell_data::e_range + spell_data::e_outer_radius) return false;

		if (!force && sdk::orbwalker && !sdk::orbwalker->combo() && !sdk::orbwalker->harass()
			&& sdk::orbwalker->would_cancel_attack()) return false;

		// Predict landing; pick a stable direction even when stacked on top of target.
		const auto predicted_pos = predict_e_landing(target, ppos);
		math::vector3 to_target = predicted_pos - ppos;
		if (ppos.distance(predicted_pos) < 10.f) to_target = tpos - ppos;
		if (to_target.x == 0.f && to_target.z == 0.f) to_target = { 1.f, 0.f, 0.f };
		const auto dir = to_target.to_direction();

		// Cage center 80% of edge gap short of predicted target — sits inside the
		const float inside_distance = (spell_data::e_outer_radius - target->get_bounding_radius()) * 0.8f;
		math::vector3 cast_pos = predicted_pos - dir * inside_distance;
		const float cage_dist = ppos.distance(cast_pos);
		if      (cage_dist < 50.f)                cast_pos = ppos + dir * 50.f;
		else if (cage_dist > spell_data::e_range) cast_pos = ppos + dir * spell_data::e_range;

		if (!is_position_pathable(cast_pos) && g_sdk->nav_mesh)
		{
			math::vector3 nearest = g_sdk->nav_mesh->get_nearest_passable_position(cast_pos);
			if (ppos.distance(nearest) <= spell_data::e_range) cast_pos = nearest;
			else return false;
		}

		bool result;
		if (!sdk::common) { player->cast_spell(SLOT_E, cast_pos); result = true; }
		else              { result = sdk::common->cast_spell(SLOT_E, cast_pos); }
		if (result) { m_last_e_cast_time = now; last_cast_e = now; }
		return result;
	}

	bool cast_r(game_object* target, bool manual)
	{
		// Save R hotkey: only semi-manual cast bypasses.
		if (!manual && menu::save_r) return false;

		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !target) return false;

		if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return false;
		if (target->has_buff_of_type(buff_type::spell_shield)) return false;

		// Anti-burst (Zilean R / Kindred R) — manual cast bypasses.
		if (!manual && venplus::utility::has_anti_burst_buff(target)) return false;

		if (sdk::common && sdk::common->spell)
		{
			if (!sdk::common->spell->is_spell_learned(SLOT_R)) return false;
			if (!sdk::common->spell->is_spell_ready(SLOT_R))   return false;
		}
		else
		{
			auto r_chk = player->get_spell(SLOT_R);
			if (!r_chk || r_chk->get_level() == 0 || r_chk->get_cooldown() > 0.f) return false;
		}

		if (player->get_par() < player->get_spell_mana_cost(SLOT_R)) return false;

		const auto  ppos = player->get_position();
		const auto  tpos = target->get_position();
		const float dist = ppos.distance(tpos);
		if (dist > spell_data::r_range) return false;

		// Yasuo wall check: R is a targeted spell but the wind-wall can still block it
		if (sdk::prediction)
		{
			pred_sdk::spell_data r_wall_chk{};
			r_wall_chk.spell_type = pred_sdk::spell_type::targetted;
			r_wall_chk.targetting_type = pred_sdk::targetting_type::center;
			r_wall_chk.spell_slot = SLOT_R;
			r_wall_chk.range = spell_data::r_range;
			r_wall_chk.radius = 0.f;
			r_wall_chk.delay = spell_data::r_cast_time;
			r_wall_chk.proc_delay = spell_data::r_cast_time;
			r_wall_chk.projectile_speed = FLT_MAX;
			r_wall_chk.source = player;
			r_wall_chk.source_position = ppos;
			r_wall_chk.forbidden_collisions = { pred_sdk::collision_type::yasuo_wall };
			try
			{
				auto coll = sdk::prediction->collides(tpos, r_wall_chk, target);
				if (coll.collided) return false;
			}
			catch (...) {}
		}

		// Lead HP prediction by cast_time + missile travel (~0.325s extra at max range).
		const float now      = g_sdk->clock_facade->get_game_time();
		const float r_travel = venplus::math::travel_time(dist, spell_data::r_missile_speed, spell_data::r_cast_time);
		float hp = target->get_hp();
		if (sdk::health_prediction)
		{
			try
			{
				const float pred = sdk::health_prediction->get_predicted_health(target, now + r_travel);
				if (pred > 0.f && pred <= target->get_max_hp() && pred <= hp)
					hp = pred;
			}
			catch (...) {}
		}

		// Magic-shield-aware effective HP (Banshee's etc.).
		const float effective_hp = hp + target->get_all_shield() + target->get_magical_shield();
		const float r_dmg = get_r_damage(target);

		if (!manual && menu::r_only_killable)
		{
			const float safety_margin = std::max(r_dmg * 0.05f, 10.f);
			if (effective_hp > (r_dmg - safety_margin)) return false;
		}

		if (sdk::orbwalker && !sdk::orbwalker->combo() && !sdk::orbwalker->harass()
			&& sdk::orbwalker->would_cancel_attack()) return false;

		if (!sdk::common) { player->cast_spell(SLOT_R, target); return true; }
		return sdk::common->cast_spell(SLOT_R, target);
	}

	float get_q_damage(game_object* target)
	{
		if (!g_sdk || !g_sdk->object_manager || !target) return 0.f;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return 0.f;

		auto q = player->get_spell(SLOT_Q);
		if (!q || q->get_level() == 0) return 0.f;

		const auto& dmg = venplus::db::champions::VEIGAR.spells[SLOT_Q].damage;
		const int rank = std::clamp(q->get_level() - 1, 0, 4);
		const float base = venplus::damage::final_spell_damage(
			dmg, rank,
			make_caster_stats(player),
			make_target_info(target),
			{}, make_magic_pen(player));
		const float distance = player->get_position().distance(target->get_position());
		const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
		meow::item_damage::ability_proc_context items{};
		items.horizon_focus_active = horizon;
		meow::item_damage::ability_rune_context runes{};
		runes.cast_distance = distance;
		return meow::item_damage::apply_magic_damage_modifiers(player, target, base, 0, horizon)
			+ meow::item_damage::on_ability_damage(player, target, items)
			+ meow::item_damage::on_ability_rune_damage(player, target, runes);
	}

	float get_w_damage(game_object* target)
	{
		if (!g_sdk || !g_sdk->object_manager || !target) return 0.f;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return 0.f;

		auto w = player->get_spell(SLOT_W);
		if (!w || w->get_level() == 0) return 0.f;

		const auto& dmg = venplus::db::champions::VEIGAR.spells[SLOT_W].damage;
		const int rank = std::clamp(w->get_level() - 1, 0, 4);
		const float base = venplus::damage::final_spell_damage(
			dmg, rank,
			make_caster_stats(player),
			make_target_info(target),
			{}, make_magic_pen(player));
		const float distance = player->get_position().distance(target->get_position());
		const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
		meow::item_damage::ability_proc_context items{};
		items.horizon_focus_active = horizon;
		meow::item_damage::ability_rune_context runes{};
		runes.cast_distance = distance;
		return meow::item_damage::apply_magic_damage_modifiers(player, target, base, 0, horizon)
			+ meow::item_damage::on_ability_damage(player, target, items)
			+ meow::item_damage::on_ability_rune_damage(player, target, runes);
	}

	float get_r_damage(game_object* target)
	{
		if (!g_sdk || !g_sdk->object_manager || !target) return 0.f;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return 0.f;

		auto r = player->get_spell(SLOT_R);
		if (!r || r->get_level() == 0) return 0.f;

		// R's missing-HP bonus is non-linear so we can't use damage_entry::miss_hp.
		float max_hp = target->get_max_hp();
		const float hp_penalty = target->get_hp_max_penalty();
		if (hp_penalty > 0.f) max_hp = std::max(0.f, max_hp - hp_penalty);
		if (max_hp <= 0.f) return 0.f;

		const float missing_pct = 1.f - (target->get_hp() / max_hp);
		const float bonus = std::min(1.5f * missing_pct, 1.f); // caps at 2× when ≤ 33% HP

		const auto& dmg_entry = venplus::db::champions::VEIGAR.spells[SLOT_R].damage;
		const int rank = std::clamp(r->get_level() - 1, 0, 2);
		const auto caster      = make_caster_stats(player);
		const auto target_info = make_target_info(target);
		const auto magic_pen   = make_magic_pen(player);

		const float raw_total = venplus::damage::raw_spell_damage(dmg_entry, rank, caster) * (1.f + bonus);
		const float eff_mr    = venplus::damage::effective_resist(target_info.mr, target_info.bonus_mr, magic_pen);
		const float base = raw_total * venplus::damage::resist_multiplier(eff_mr);
		const float distance = player->get_position().distance(target->get_position());
		const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
		meow::item_damage::ability_proc_context items{};
		items.horizon_focus_active = horizon;
		meow::item_damage::ability_rune_context runes{};
		runes.cast_distance = distance;
		return meow::item_damage::apply_magic_damage_modifiers(player, target, base, 0, horizon)
			+ meow::item_damage::on_ability_damage(player, target, items)
			+ meow::item_damage::on_ability_rune_damage(player, target, runes);
	}

	// Base R damage ( missing- bonus) — used for the conservative HP-bar indicator.
	float get_r_base_damage(game_object* target)
	{
		if (!g_sdk || !g_sdk->object_manager || !target) return 0.f;
		auto player = g_sdk->object_manager->get_local_player();
		if (!player) return 0.f;

		auto r = player->get_spell(SLOT_R);
		if (!r || r->get_level() == 0) return 0.f;

		const auto& dmg = venplus::db::champions::VEIGAR.spells[SLOT_R].damage;
		const int rank = std::clamp(r->get_level() - 1, 0, 2);
		const float base = venplus::damage::final_spell_damage(
			dmg, rank,
			make_caster_stats(player),
			make_target_info(target),
			{}, make_magic_pen(player));
		const bool horizon = player->get_position().distance(target->get_position())
			>= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
		return meow::item_damage::apply_magic_damage_modifiers(player, target, base, 0, horizon);
	}

	void cast_q_after_w()
	{
		if (!w_track.active) return;
		if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || player->is_dead()) return;

		if (!sdk::orbwalker || !sdk::orbwalker->combo()) return;
		if (!menu::use_q_combo) return;
		if (is_casting()) return;

		const float now = g_sdk->clock_facade->get_game_time();

		if (now > w_track.impact_time + 0.1f) { w_track.active = false; return; }
		if (now < w_track.impact_time - 0.1f) return; // wait until W is about to land

		if (!is_spell_learned(SLOT_Q) || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q))
			return;

		const auto ppos = player->get_position();

		for (auto hero : g_sdk->object_manager->get_heroes())
		{
			if (!is_valid_target(hero, spell_data::q_range)) continue;

			const auto hpos = hero->get_position();
			if (w_track.pos.distance(hpos) > spell_data::w_radius + hero->get_bounding_radius()) continue;

			if (!sdk::prediction) continue;

			pred_sdk::spell_data q{};
			q.spell_type = pred_sdk::spell_type::linear;
			q.targetting_type = pred_sdk::targetting_type::edge_to_edge;
			q.spell_slot = SLOT_Q;
			q.range = spell_data::q_range;
			q.radius = spell_data::q_radius;
			q.delay = spell_data::q_delay;
			q.proc_delay = spell_data::q_delay;
			q.projectile_speed = spell_data::q_speed;
			q.source = player;
			q.source_position = ppos;

			const int  player_level = player->get_level();
			const bool no_minion_col = menu::q_no_minion_collision_enabled && player_level >= menu::q_no_minion_collision_level;
			if (no_minion_col)
				q.forbidden_collisions = { pred_sdk::collision_type::unit, pred_sdk::collision_type::yasuo_wall, pred_sdk::collision_type::braum_wall };
			else
				q.forbidden_collisions = { pred_sdk::collision_type::yasuo_wall, pred_sdk::collision_type::braum_wall };

			q.expected_hitchance = 35;

			try
			{
				auto res = sdk::prediction->predict(hero, q);
				if (!res.is_valid || res.hitchance < 35) continue;

				bool ok = false;
				if (!sdk::common) { player->cast_spell(SLOT_Q, res.cast_position); ok = true; }
				else ok = sdk::common->cast_spell(SLOT_Q, res.cast_position);
				if (ok) { m_last_q_cast_time = now; return; }
			}
			catch (...) {}
		}
	}

	bool can_r_kill_target(game_object* target)
	{
		if (!target || !g_sdk || !g_sdk->object_manager)
			return false;

		auto player = g_sdk->object_manager->get_local_player();
		if (!player || !sdk::damage)
			return false;

		if (!target->is_valid() || target->is_dead() || !target->is_targetable())
			return false;

		const float r_damage = get_r_damage(target);
		if (r_damage <= 0.f) return false;

		float hp = target->get_hp();
		if (sdk::health_prediction && g_sdk->clock_facade)
		{
			const float dist     = player->get_position().distance(target->get_position());
			const float r_travel = venplus::math::travel_time(dist, spell_data::r_missile_speed, spell_data::r_cast_time);
			const float now      = g_sdk->clock_facade->get_game_time();
			try
			{
				const float pred = sdk::health_prediction->get_predicted_health(target, now + r_travel);
				if (pred > 0.f && pred <= target->get_max_hp() && pred <= hp)
					hp = pred;
			}
			catch (...) {}
		}

		const float effective_hp = hp + target->get_all_shield() + target->get_magical_shield();

		// If Q alone closes the kill, save R for harder targets.
		if (is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
		{
			const float q_damage = get_q_damage(target);
			if (q_damage > 0.f && q_damage >= effective_hp) return false;
		}

		if (r_damage >= effective_hp) return true;

		const float aa_damage = sdk::damage->get_aa_damage(player, target, false);
		if (aa_damage > 0.f && (r_damage + aa_damage * 2.f) >= effective_hp) return true;

		if (is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
		{
			const float q_damage = get_q_damage(target);
			if (q_damage > 0.f && (r_damage + q_damage) >= effective_hp) return true;
		}

		return false;
	}

	static void setup_menu()
	{
		auto cat = g_sdk->menu_manager->add_category("veigar", "MeowAIO - Veigar");
		if (!cat) return;

		// Q
		auto q_menu = cat->add_sub_category("q_baleful_strike", "Q - Baleful Strike");
		q_menu->add_checkbox("combo", "Use in Combo", menu::use_q_combo, [](bool v) { menu::use_q_combo = v; });
		q_menu->add_checkbox("harass", "Use in Harass", menu::use_q_harass, [](bool v) { menu::use_q_harass = v; });
		q_menu->add_checkbox("lasthit", "Lasthit", menu::use_q_lasthit, [](bool v) { menu::use_q_lasthit = v; });
		q_menu->add_checkbox("double_lasthit", "Try To Double Lasthit", menu::use_q_double_lasthit, [](bool v) { menu::use_q_double_lasthit = v; });
		q_menu->add_hotkey("auto_q_farm_key", "Auto Q Farm", 'J', false, false,
			[](std::string* key, bool val)
			{
				menu::auto_q_farm_key = key;
				if (val) // key-down edge
					menu::auto_q_farm = !menu::auto_q_farm;
			});
		q_menu->add_checkbox("clear", "Use in Clear", menu::use_q_clear, [](bool v) { menu::use_q_clear = v; });
		q_menu->add_checkbox("jungle", "Use in Jungle", menu::use_q_jungle, [](bool v) { menu::use_q_jungle = v; });
		q_menu->add_checkbox("ks", "Killsteal", menu::ks_q, [](bool v) { menu::ks_q = v; });
		q_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::q_hitchance,
			[](int v) { menu::q_hitchance = v; });
		q_menu->add_checkbox("no_minion_collision_enabled", "Remove Minion Collision", menu::q_no_minion_collision_enabled,
			[](bool v) { menu::q_no_minion_collision_enabled = v; });
		q_menu->add_slider_int("no_minion_collision_level", "Remove Minion Collision at Level", 1, 18, 1, menu::q_no_minion_collision_level,
			[](int v) { menu::q_no_minion_collision_level = v; });
		q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
			[](bool v) { menu::draw_q_range = v; });

		// W
		auto w_menu = cat->add_sub_category("w_dark_matter", "W - Dark Matter");
		w_menu->add_checkbox("combo", "Use in Combo", menu::use_w_combo, [](bool v) { menu::use_w_combo = v; });
		w_menu->add_checkbox("harass", "Use in Harass", menu::use_w_harass, [](bool v) { menu::use_w_harass = v; });
		w_menu->add_checkbox("ks", "Killsteal", menu::ks_w, [](bool v) { menu::ks_w = v; });
		w_menu->add_checkbox("auto_cc", "Auto W on CC", menu::auto_w_on_cc, [](bool v) { menu::auto_w_on_cc = v; });
		w_menu->add_checkbox("auto_cage", "Auto W in E Cage", menu::auto_w_in_cage, [](bool v) { menu::auto_w_in_cage = v; });
		w_menu->add_separator();
		w_menu->add_checkbox("clear", "Use in Clear", menu::use_w_clear, [](bool v) { menu::use_w_clear = v; });
		w_menu->add_checkbox("jungle", "Use in Jungle", menu::use_w_jungle, [](bool v) { menu::use_w_jungle = v; });
		w_menu->add_slider_int("min_minions", "Clear Min Minions", 1, 10, 1, menu::w_clear_min_minions,
			[](int v) { menu::w_clear_min_minions = v; });
		w_menu->add_separator();
		w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::w_hitchance,
			[](int v) { menu::w_hitchance = v; });
		w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
			[](bool v) { menu::draw_w_range = v; });
		w_menu->add_hotkey("semi_w", "Semi-Manual W", 'T', false, false,
			[](std::string* key, bool val) { menu::semi_w_active = val; menu::semi_w_key = key; });

		// E
		auto e_menu = cat->add_sub_category("e_event_horizon", "E - Event Horizon");
		e_menu->add_checkbox("auto_stun", "Auto E on CC", menu::auto_e_on_stun, [](bool v) { menu::auto_e_on_stun = v; });
		e_menu->add_checkbox("prioritize_aoe", "Prioritize AOE", menu::e_prioritize_aoe, [](bool v) { menu::e_prioritize_aoe = v; });
		e_menu->add_checkbox("antigap", "Anti Gap-Close", menu::use_e_antigap, [](bool v) { menu::use_e_antigap = v; });
		e_menu->add_separator();
		e_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::e_hitchance,
			[](int v) { menu::e_hitchance = v; });
		e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
			[](bool v) { menu::draw_e_range = v; });
		e_menu->add_hotkey("semi_e", "Semi-Manual E Center", 'G', false, false,
			[](std::string* key, bool val) { menu::semi_e_active = val; menu::semi_e_key = key; });
		e_menu->add_hotkey("semi_e_edge", "Semi-Manual E Edge Stun", 'H', false, false,
			[](std::string* key, bool val) { menu::semi_e_edge_active = val; menu::semi_e_edge_key = key; });
		e_menu->add_separator();
		auto e_wl_menu = e_menu->add_sub_category("e_whitelist", "Whitelist");
		{
			auto local = g_sdk->object_manager->get_local_player();
			if (local)
			{
				for (auto hero : g_sdk->object_manager->get_heroes())
				{
					if (!hero || !hero->is_valid()) continue;
					if (hero->get_team_id() == local->get_team_id()) continue;

					const std::string name = hero->get_char_name();
					if (name.empty()) continue;
					if (menu::e_whitelist.count(name)) continue;

					menu::e_whitelist[name] = true;
					e_wl_menu->add_checkbox("wl_" + name, name,
						menu::e_whitelist.at(name),
						[name](bool v) { menu::e_whitelist[name] = v; });
				}
			}

			if (menu::e_whitelist.empty())
				e_wl_menu->add_label("No enemies detected — join a game and reload.");
		}

		// R
		auto r_menu = cat->add_sub_category("r_primordial_burst", "R - Primordial Burst");
		r_menu->add_checkbox("combo", "Use in Combo", menu::use_r_combo, [](bool v) { menu::use_r_combo = v; });
		r_menu->add_checkbox("ks", "Killsteal", menu::ks_r, [](bool v) { menu::ks_r = v; });
		r_menu->add_hotkey("save_r_key", "Save R", 'U', false, false,
			[](std::string* key, bool val)
			{
				menu::save_r_key = key;
				if (val)
					menu::save_r = !menu::save_r;
			});
		r_menu->add_checkbox("only_kill", "Only if Killable", menu::r_only_killable, [](bool v) { menu::r_only_killable = v; });
		r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
			[](bool v) { menu::draw_r_range = v; });

		// Mana
		auto mana_menu = cat->add_sub_category("mana", "Mana Thresholds");
		mana_menu->add_slider_int("harass_mana", "Harass Mana %", 0, 100, 5, menu::harass_mana_percent,
			[](int v) { menu::harass_mana_percent = v; });
		mana_menu->add_slider_int("farm_mana", "Farm Mana %", 0, 100, 5, menu::farm_mana_percent,
			[](int v) { menu::farm_mana_percent = v; });
		mana_menu->add_slider_int("jungle_mana", "Jungle Mana %", 0, 100, 5, menu::jungle_mana_percent,
			[](int v) { menu::jungle_mana_percent = v; });

		// Drawings
		auto draw_menu = cat->add_sub_category("drawings", "Drawings");
		draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready, [](bool v) { menu::draw_only_ready = v; });
		draw_menu->add_checkbox("r_killable", "R Killable Text", menu::draw_r_killable, [](bool v) { menu::draw_r_killable = v; });
		draw_menu->add_checkbox("r_damage_indicator", "R Damage Indicator", menu::draw_r_damage_indicator, [](bool v) { menu::draw_r_damage_indicator = v; });
		draw_menu->add_checkbox("draw_e_cage", "Draw E Cage", menu::draw_e_cage, [](bool v) { menu::draw_e_cage = v; });
		draw_menu->add_checkbox("draw_stacks", "Draw Passive Stacks", menu::draw_stacks, [](bool v) { menu::draw_stacks = v; });
		draw_menu->add_separator();
		draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q, [](uint32_t c) { menu::color_q = c; });
		draw_menu->add_colorpicker("color_w", "W Color", menu::color_w, [](uint32_t c) { menu::color_w = c; });
		draw_menu->add_colorpicker("color_e", "E Color", menu::color_e, [](uint32_t c) { menu::color_e = c; });
		draw_menu->add_colorpicker("color_r", "R Color", menu::color_r, [](uint32_t c) { menu::color_r = c; });
		draw_menu->add_colorpicker("color_r_dmg", "R Damage Bar Color", menu::color_r_dmg, [](uint32_t c) { menu::color_r_dmg = c; });
	}

	static void setup_infotab()
	{
		if (!sdk::infotab) return;

		if (menu::auto_q_farm_key)
			menu::infotab_auto_q_farm = sdk::infotab->add_hotkey_text(
				menu::auto_q_farm_key, { "Auto Q Farm" },
				[]() -> infotab_sdk::text_entry {
					return { menu::auto_q_farm ? "ON" : "OFF",
							 menu::auto_q_farm ? 0xFF00FF00u : 0xFFFF0000u };
				});
		if (menu::save_r_key)
			menu::infotab_save_r = sdk::infotab->add_hotkey_text(
				menu::save_r_key, { "Save R" },
				[]() -> infotab_sdk::text_entry {
					return { menu::save_r ? "ON" : "OFF",
							 menu::save_r ? 0xFF00FF00u : 0xFFFF0000u };
				});
		if (menu::semi_w_key)
			menu::infotab_semi_w = sdk::infotab->add_hotkey_text(
				menu::semi_w_key, { "Semi W" },
				[]() -> infotab_sdk::text_entry {
					return { menu::semi_w_active ? "ON" : "OFF",
							 menu::semi_w_active ? 0xFF00FF00u : 0xFFFF0000u };
				});
		if (menu::semi_e_key)
			menu::infotab_semi_e = sdk::infotab->add_hotkey_text(
				menu::semi_e_key, { "Semi E Center" },
				[]() -> infotab_sdk::text_entry {
					return { menu::semi_e_active ? "ON" : "OFF",
							 menu::semi_e_active ? 0xFF00FF00u : 0xFFFF0000u };
				});
		if (menu::semi_e_edge_key)
			menu::infotab_semi_e_edge = sdk::infotab->add_hotkey_text(
				menu::semi_e_edge_key, { "Semi E Edge" },
				[]() -> infotab_sdk::text_entry {
					return { menu::semi_e_edge_active ? "ON" : "OFF",
							 menu::semi_e_edge_active ? 0xFF00FF00u : 0xFFFF0000u };
				});
	}

} // namespace veigar
