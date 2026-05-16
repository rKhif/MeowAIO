#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace zaahen
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::zaahen.hpp
    // (canonical spell data). Names are kept in our snake_case style.
    // ============================================================================
    namespace spell_data
    {
        // ---- Passive: Cultivation of War / Determination ----
        constexpr int   passive_max_stacks    = 12;
        constexpr float passive_stack_dur     = 5.f;       
        constexpr float passive_revive_dur    = 4.f;       
        constexpr float passive_revive_cd[4]  = { 300.f, 240.f, 180.f, 120.f };
        constexpr float passive_revive_heal[4]= { 0.30f, 0.45f, 0.60f, 0.75f };
        constexpr float passive_ad_per_stack_lo = 0.015f;
        constexpr float passive_ad_per_stack_hi = 0.0295f;

        // ---- Q: The Darkin Glaive — empowered AA, 2-part with recast knockup ----
        constexpr float q_range_bonus       = 25.f;        // +25 AA range during Q
        constexpr float q_empower_window    = 4.f;         // time to AA after Q1 before empower expires
        constexpr float q_recast_min_delay  = 1.5f;        // earliest recast time after Q1
        constexpr float q_recast_window     = 4.f;         // recast must happen within this after min_delay
        constexpr float q_knockup_dur       = 0.75f;       // Q2 knockup duration (CC follow-up window)
        constexpr float q_monster_mult      = 2.f;         // 200% damage to monsters
        constexpr float q_heal_minion_mult  = 0.5f;        // heal halved on minion/monster targets
        constexpr float q_heal_pct[5]       = { 0.05f, 0.06f, 0.07f, 0.08f, 0.09f };

        // ---- W: Dreaded Return — linear skillshot, terminal stun + pull ----
        constexpr float w_range         = 850.f;
        constexpr float w_radius        = 35.f;            // 70 width / 2 (pred wants half-width)
        constexpr float w_speed         = 2900.f;          // outgoing projectile
        constexpr float w_speed_return  = 900.f;           // return-pull speed
        constexpr float w_delay         = 0.5f;            // cast time
        constexpr float w_stun_dur      = 0.25f;           // terminal-hit stun
        constexpr float w_pull_dist     = 225.f;           // pulled toward player

        // ---- E: Aureate Rush — dash + AOE, sweet-spot at outer ring ----
        constexpr float e_range             = 350.f;
        constexpr float e_inner_radius      = 200.f;
        constexpr float e_outer_radius      = 375.f;
        constexpr float e_dash_speed        = 900.f;
        constexpr float e_outer_phys_mult   = 1.5f;        // +50% physical at outer ring
        constexpr float e_monster_bonus     = 50.f;        // flat bonus damage on monsters
        // Outer-ring magic %max-HP damage per rank (capped at 400 vs monsters)
        constexpr float e_max_hp_dmg[5]     = { 0.04f, 0.045f, 0.05f, 0.055f, 0.06f };
        constexpr float e_max_hp_monster_cap = 400.f;

        // ---- R: Grim Deliverance — dash + delayed slam ----
        constexpr float r_range        = 600.f;
        constexpr float r_radius       = 550.f;
        constexpr float r_speed        = 2800.f;           // dash speed
        constexpr float r_cast_time    = 0.5f;
        constexpr float r_slam_delay   = 0.6f;
        constexpr float r_dmg_reduction = 0.5f;            // 50% dmg taken reduction during cast/slam
        constexpr float r_armor_pen[3]   = { 0.10f, 0.20f, 0.30f };       // passive %armor pen per rank
        constexpr float r_heal_per_champ[3] = { 82.5f, 132.f, 181.5f };   // base heal per hit
        constexpr float r_heal_bonus_ad     = 0.66f;       // +66% bonus AD per hit
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool use_q_combo = true;
        inline bool use_w_combo = true;
        inline bool use_e_combo = true;
        inline bool use_q_recast_combo = true;
        inline bool use_r_combo = true;

        // Q firing mode:
        //  0 = "Before Attack"  — Q replaces the next AA (empowered AA fires immediately)
        //  1 = "AA Reset After" — let normal AA fire first, then Q to reset for second AA
        //  2 = "Instant"        — fire Q1 / Q2 the moment they're up, any orbwalker mode
        inline int  q_mode = 0;

        // Combo spell ordering:
        //  false = Normal     — W first, then E (poke / catch / close)
        //  true  = Teamfight  — E first, then W (engage AOE / catch follow-up)
        // Toggled by the in-menu hotkey OR set by the checkbox.
        inline bool combo_teamfight = false;


        // --- E sweet-spot aiming ---
        // When true, cast_e aims so target ends up in the outer ring (sweet-spot damage).
        inline bool e_sweetspot = true;

        // --- R gating ---
        inline bool r_only_killable = false;
        // Only fire R when it would actually hit >= r_min_targets enemy heroes
        // inside the slam radius (centered on target landing pos, +25u slack).
        // Mirrors the pattern from zeri.cpp::try_r — guards against the target
        // dashing out of slam range during the 0.5+0.6s wind-up.
        inline bool r_require_hits = true;
        inline int  r_min_targets  = 1;
        // Only fire R when passive Determination is at max stacks (12) — peak
        // bonus AD multiplier roughly doubles R damage. Burst-window gate.
        inline bool r_only_max_stacks = false;

        // Block the user's own R press (event::cast_spell) when the click position
        // wouldn't catch >= r_block_min_hits enemies in the slam radius. Mirrors
        // zeri.cpp's manual-R gate. Bypassed by the Semi-R hotkey (explicit override).
        inline bool r_block_manual    = true;
        inline int  r_block_min_hits  = 1;

        // --- Semi-R (manual hold-to-cast) ---
        // semi_r_active is driven by the bound hotkey (HOLD style).
        // Bypasses r_only_killable + r_min_targets gates — user override.
        inline bool        semi_r_active = false;
        inline std::string semi_r_key_str;
        inline uint32_t    infotab_semi_r = 0;

        // --- Semi-W (manual hold-to-cast skillshot) ---
        // Fires W on best hero in range; uses prediction. HOLD-style.
        inline bool        semi_w_active = false;
        inline std::string semi_w_key_str;
        inline uint32_t    infotab_semi_w = 0;

        // --- Flash + E sweet-spot + W combo macro ---
        // Rising-edge hotkey: one press triggers Flash → wait → E sweet-spot → wait → W.
        inline std::string flash_ew_key_str;
        inline uint32_t    infotab_flash_ew = 0;

        // --- Farm (clear / fast-clear) ---
        inline bool use_q_clear      = true;   // Q empower on minion AAs (clear mode)
        inline bool use_w_clear      = true;
        inline bool use_e_clear      = false;  // E dash-AOE wave/jungle clear (off by default — repositions player)
        inline bool use_q_fast_clear = true;
        inline bool use_w_fast_clear = true;
        inline bool use_e_fast_clear = true;
        inline int  clear_mana_pct   = 30;     // skip W/E in clear when mana% below this
        inline int  clear_min_hits   = 3;      // W aoe target count for clear
        inline int  e_clear_min_hits = 3;      // E AOE target count for clear

        // --- W prediction ---
        inline int  w_hitchance = 50; // 0-100; W is wide/fast, medium is the right default

        // W cast logging — emits per-cast diagnostic lines to the console.
        // Off by default (chatty); flip on when tuning hitchance / predict.
        inline bool w_logging = false;

        // --- Drawings ---
        inline bool draw_q_range     = true;
        inline bool draw_w_range     = true;
        inline bool draw_e_range     = true;
        inline bool draw_r_range     = true;
        inline bool draw_e_sweetspot = true;  // shows inner/outer ring of E
        inline bool draw_e_terrain   = false; // dot markers showing pathable / blocked dash landings
        inline bool draw_only_ready  = false; // hide circle when spell is on CD/unlearned
        inline bool draw_r_dmg_bar      = true;   // dedicated R-only HP-bar overlay
        inline bool draw_combo_mode = true;       // text label above player showing combo mode
        inline bool draw_q_mode     = true;       // text label above player showing Q firing mode
        inline bool draw_passive_stacks = true;   // Determination stack count above player
        inline bool draw_r_killable     = true;   // floating "R KILL" label above killable enemies

        // --- Colors (ARGB) ---
        inline uint32_t color_q = 0xFFFFD700u; // gold
        inline uint32_t color_w = 0xFF87CEEBu; // sky blue
        inline uint32_t color_e = 0xFF00FF7Fu; // spring green
        inline uint32_t color_r = 0xFFFF4500u; // orange-red
        inline uint32_t color_r_dmg = 0x90FF4500u; // orange-red w/ alpha (R dmg bar)
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load();
    void unload();

    // ============================================================================
    // EVENT CALLBACKS - __fastcall per SDK requirement
    // ============================================================================
    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_present();
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_execute_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_new_path(game_object* obj, bool is_dash, float dash_speed);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
    void __fastcall on_spell_hit(game_object* obj, spell_cast* cast);
    bool __fastcall on_cast_spell(game_object* obj, int spell_slot,
                                  math::vector3 position, game_object* target);
    bool __stdcall  on_before_attack(orb_sdk::event_data* data);

    // ============================================================================
    // GAME MODE FUNCTIONS
    // ============================================================================
    void combo();
    void clear();
    void fast_clear();

    // ============================================================================
    // TARGETING
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);

    // ============================================================================
    // SPELL CAST FUNCTIONS
    // ============================================================================
    bool cast_q(game_object* target);          // Q1 - empowered AA on target
    bool cast_q_recast();                      // Q2 - knockup self-cast
    bool cast_w(game_object* target, bool from_semi = false);
    bool cast_e(game_object* target);          // dash, sweet-spot aware
    bool cast_r(game_object* target, bool force = false);  // dash + slam; force bypasses gates

    // ============================================================================
    // SPELL VALIDATION HELPERS
    // ============================================================================
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    bool is_casting();
    float aa_range_to(game_object* target);
}
