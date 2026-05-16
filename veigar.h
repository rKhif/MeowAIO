#pragma once

#include "sdk.hpp"
#include <string>
#include <unordered_map>

namespace veigar
{
	// ============================================================================
	// SPELL SLOT CONSTANTS
	// ============================================================================
	constexpr int SLOT_Q = 0;
	constexpr int SLOT_W = 1;
	constexpr int SLOT_E = 2;
	constexpr int SLOT_R = 3;

	// ============================================================================
	// SPELL DATA — sourced from meow_shared/db/champions/veigar.hpp (canonical).
	// ============================================================================
	namespace spell_data
	{
		// Q - Baleful Strike: linear skillshot, blocked by units
		constexpr float q_range  = 1050.f;
		constexpr float q_radius = 70.f;    // half of 140 total width
		constexpr float q_speed  = 2200.f;
		constexpr float q_delay  = 0.25f;   // cast animation

		// W - Dark Matter: circular AoE.
		constexpr float w_range      = 950.f;
		constexpr float w_radius     = 240.f;
		constexpr float w_cast_time  = 0.25f;
		constexpr float w_proc_delay = 1.221f;
		constexpr float w_delay      = w_cast_time + w_proc_delay;

		// E - Event Horizon: ring cage
		constexpr float e_range        = 725.f;
		constexpr float e_outer_radius = 411.f;
		constexpr float e_inner_radius = 290.f;
		constexpr float e_delay        = 0.75f; // cast 0.25 + formation 0.5

		// R - Primordial Burst: targeted execute
		constexpr float r_range         = 650.f;
		constexpr float r_cast_time     = 0.25f;
		constexpr float r_missile_speed = 2000.f;
	}

	// ============================================================================
	// PREDICTION CONSTANTS
	// ============================================================================
	namespace prediction_constants
	{
		constexpr float min_hp_difference = 5.f;
		constexpr float stationary_speed_threshold = 10.f;
		constexpr float fast_movement_threshold = 400.f;
		constexpr float movement_direction_threshold = 0.3f;
	}

	// ============================================================================
	// MENU VARIABLES
	// ============================================================================
	namespace menu
	{
		// --- Combo ---
		inline bool use_q_combo = true;
		inline bool use_w_combo = true;
		inline bool use_r_combo = true;

		// --- Harass ---
		inline bool use_q_harass = true;
		inline bool use_w_harass = false;
		inline int  harass_mana_percent = 50;

		// --- Farm / Lasthit ---
		inline bool use_q_lasthit = true;
		inline bool use_q_double_lasthit = true; // Try to lasthit 2 minions with Q for double stacks
		inline bool auto_q_farm = false; // Auto-use Q for farm logic while keybind is active
		inline std::string* auto_q_farm_key = nullptr;
		inline bool use_q_clear = true;
		inline bool use_q_jungle = true;
		inline bool use_w_clear = true;
		inline bool use_w_jungle = true;
		inline int  w_clear_min_minions = 3;
		inline int  farm_mana_percent = 30;
		inline int  jungle_mana_percent = 20;

		// --- Killsteal ---
		inline bool ks_q = true;
		inline bool ks_w = true;
		inline bool ks_r = true;

		// --- Hitchance (slider values 0-100) ---
		inline int  q_hitchance = 50;  // Hitchance slider (0-100)
		inline int w_hitchance = 70; // Hitchance slider (0-100)
		inline int e_hitchance = 30; // Hitchance slider (0-100)

		// --- Q minion collision ---
		inline bool q_no_minion_collision_enabled = false; // Enable/disable removing minion collision
		inline int  q_no_minion_collision_level = 18; // Remove minion collision at this level (1-18)

		// --- E anti-gapclose ---
		inline bool use_e_antigap = true;
		static constexpr float ANTIGAP_MIN_SPEED = 200.f;

		// --- E AOE ---
		inline bool e_prioritize_aoe = false;

		// --- E whitelist ---
		inline std::unordered_map<std::string, bool> e_whitelist;

		// --- W auto on CC ---
		inline bool auto_w_on_cc = true;
		inline bool auto_w_in_cage = true;

		// --- E auto on stun ---
		inline bool auto_e_on_stun = true;

		// --- R settings ---
		inline bool         save_r = false;
		inline std::string* save_r_key = nullptr;
		inline bool r_only_killable = false;

		// --- Semi-manual keys ---
		inline bool         semi_w_active = false;
		inline std::string* semi_w_key = nullptr;
		inline bool         semi_e_active = false;
		inline std::string* semi_e_key = nullptr;
		inline bool         semi_e_edge_active = false;
		inline std::string* semi_e_edge_key = nullptr;

		// --- Drawing ---
		inline bool draw_q_range = true;
		inline bool draw_w_range = true;
		inline bool draw_e_range = true;
		inline bool draw_r_range = true;
		inline bool draw_only_ready = false;
		inline bool draw_r_killable = true;
		inline bool draw_r_damage_indicator = true;
		inline bool draw_e_cage = true;
		inline bool draw_stacks = true;

		// --- Colors (ARGB) ---
		inline uint32_t color_q = 0xFFFFD700u; // gold
		inline uint32_t color_w = 0xFF9400D3u; // violet
		inline uint32_t color_e = 0xFF00CED1u; // dark turquoise
		inline uint32_t color_r = 0xFFFF4500u; // orange-red
		inline uint32_t color_r_dmg = 0x90FF4500u; // orange-red w/ alpha (R dmg bar)

		// --- Infotab handles ---
		inline uint32_t infotab_auto_q_farm = 0;
		inline uint32_t infotab_save_r = 0;
		inline uint32_t infotab_semi_w = 0;
		inline uint32_t infotab_semi_e = 0;
		inline uint32_t infotab_semi_e_edge = 0;
	}

	// ============================================================================
	// LIFECYCLE
	// ============================================================================
	void load();
	void unload();

	// ============================================================================
	// EVENT CALLBACKS � __fastcall per SDK requirement
	// ============================================================================
	void __fastcall on_update();
	void __fastcall on_draw();
	void __fastcall on_present();
	void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
	bool __stdcall  on_before_attack(orb_sdk::event_data* data);

	// ============================================================================
	// TARGETING
	// ============================================================================
	bool is_valid_target(game_object* unit, float range = FLT_MAX);

	// ============================================================================
	// GAME MODE FUNCTIONS
	// ============================================================================
	void combo();
	void harass();
	void lasthit(bool allow_any_mode = false);
	void clear();
	void killsteal();

	// ============================================================================
	// ANTI-GAPCLOSE
	// ============================================================================
	void anti_gap_e();

	// ============================================================================
	// AUTO W ON CC
	// ============================================================================
	void auto_w_on_cc();
	void auto_w_in_cage();

	// ============================================================================
	// AUTO E ON STUN
	// ============================================================================
	void auto_e_on_stun();

	// ============================================================================
	// STASIS DETECTION
	// ============================================================================
	// Fires E at enemies exiting stasis (Zhonya's, GA, Bard R) — guaranteed hit.
	void stasis_e();

	// ============================================================================
	// SPELL CAST FUNCTIONS
	// ============================================================================
	bool cast_q(game_object* target);
	bool cast_w(game_object* target, bool force = false);

	// cast_e: places cage at predicted enemy position.
	// anti_gapclose = true   → cage border BETWEEN player and target.
	// force         = true   → bypasses would_cancel_attack (semi-manual keys).
	// wall_on_target = true  → at <550u shift cage so the stun wall lands on the
	//                          target (edge-stun, default for combo). Set false
	//                          for "center on target" semantics (Semi E Center).
	bool cast_e(game_object* target, bool anti_gapclose = false, bool force = false,
		bool wall_on_target = true);

	// cast_e_edge_stun: places cage so enemy is at the edge (for edge stun)
	bool cast_e_edge_stun(game_object* target, bool force = false);

	// cast_r: targeted execute. manual = true bypasses r_only_killable check.
	bool cast_r(game_object* target, bool manual = false);

	// ============================================================================
	// DAMAGE CALCULATION / HELPERS
	// ============================================================================
	float get_q_damage(game_object* target);
	float get_w_damage(game_object* target);
	float get_r_damage(game_object* target);
	float get_r_base_damage(game_object* target); // Base R damage only, no missing-HP bonus (for HP bar indicator)
	void  cast_q_after_w();                        // Q on targets about to be hit by active W

	// Combo-aware R kill check (similar to Kassadin can_r_kill_target)
	bool can_r_kill_target(game_object* target);

	// Generic spell helpers (patterned after Kassadin)
	bool is_spell_learned(int slot);
	bool is_spell_ready(int slot);
	bool has_enough_mana(int slot);
	bool is_casting();
}
