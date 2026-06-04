#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace riven
{
	constexpr int SLOT_Q = 0;
	constexpr int SLOT_W = 1;
	constexpr int SLOT_E = 2;
	constexpr int SLOT_R = 3;

	namespace spell_data
	{
		constexpr float q_range = 260.f;
		constexpr float q3_range = 290.f;
		constexpr float q_radius = 112.5f;
		constexpr float q3_radius = 120.f;
		constexpr float q_delay = 0.25f;

		constexpr float w_range = 300.f;
		constexpr float w_r_range = 375.f;

		constexpr float e_range = 250.f;

		constexpr float r_range = 1100.f;
		constexpr float r_radius = 100.f;
		constexpr float r_speed = 1600.f;
		constexpr float r_delay = 0.25f;
	}

	namespace menu
	{
		inline bool combo_use_q = true;
		inline bool combo_use_w = true;
		inline bool combo_use_e = true;
		inline bool combo_use_r2 = true;
		inline bool combo_r2_only_killable = true;
		inline bool combo_e_gapclose = true;
		inline bool combo_q_gapclose = true;
		inline bool combo_e_outside_aa = true;

		inline bool harass_use_q = true;
		inline bool harass_use_w = false;
		inline bool harass_use_e = false;
		inline int harass_mana_pct = 35;

		inline bool clear_use_q = true;
		inline bool clear_use_w = true;
		inline int clear_w_min_minions = 3;
		inline int clear_mana_pct = 20;

		inline bool ks_r2 = true;
		inline bool flee_use_q = true;
		inline bool flee_use_e = true;

		inline bool prep_r1 = false;
		inline bool semi_r2 = false;
		inline std::string prep_r1_key_str;
		inline std::string semi_r2_key_str;
		inline uint32_t infotab_prep_r1 = 0;
		inline uint32_t infotab_semi_r2 = 0;

		inline bool draw_q_range = true;
		inline bool draw_w_range = true;
		inline bool draw_e_range = true;
		inline bool draw_r_range = true;
		inline bool draw_only_ready = false;
		inline bool draw_damage_bar = true;
		inline bool draw_q_text = true;
		inline bool draw_q_timer = true;
		inline uint32_t color_q = 0xFF80FFF0u;
		inline uint32_t color_w = 0xFFFFC040u;
		inline uint32_t color_e = 0xFF80B0FFu;
		inline uint32_t color_r = 0xFFFF6060u;
	}

	void load();
	void unload();

	void __fastcall on_update();
	void __fastcall on_draw();
	void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
	void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
	void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
	bool __stdcall on_after_attack(orb_sdk::event_data* data);

	void combo();
	void harass();
	void clear();
	void flee();

	bool is_spell_learned(int slot);
	bool is_spell_ready(int slot);
	bool has_enough_resource(int slot);
	bool is_casting();
	float mana_pct();
	bool is_valid_target(game_object* unit, float range = FLT_MAX);
}
