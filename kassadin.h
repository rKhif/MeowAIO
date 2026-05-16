#pragma once

#include "sdk/sdk.hpp"
#include "Module.h"

class KassadinModule : public Module {
public:
    KassadinModule();
    ~KassadinModule() override = default;

    // Module interface
    bool initialize(menu_category* parent_menu) override;
    void shutdown() override;
    bool is_enabled() const override { return m_enabled; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }
    const char* get_name() const override { return "kassadin"; }
    const char* get_display_name() const override { return "XD Kassadin"; }

    void on_load();
    void on_unload();

private:
    static KassadinModule* s_instance;

    // Module state
    bool m_enabled = true;

    // Spell data structure
    struct spell_info {
        float range;
        float radius;
        float delay;
        float missile_speed;
        bool is_ready;
        float cooldown;
        float mana_cost;
        
        void update_data(game_object* local_player, int spell_slot);
        bool can_cast(game_object* local_player, int spell_slot) const;
        bool is_in_range(game_object* target, game_object* local_player) const;
    };

    // Spell data
    spell_info m_q_info;  // Null Sphere
    spell_info m_w_info;  // Nether Blade
    spell_info m_e_info;  // Force Pulse
    spell_info m_r_info;  // Riftwalk (não usado por enquanto)

    // Cast timing control (anti-spam)
    float m_last_spell_cast_time;
    static constexpr float MIN_SPELL_DELAY = 0.1f; // 100ms entre casts

    // Kassadin AA animation hashes
    static constexpr uint32_t KASSADIN_AA_HASH_1 = 0x9DD9DC06;
    static constexpr uint32_t KASSADIN_AA_HASH_2 = 0x56B1E924;

    // W spell weaving state (scheduled cast during AA animation)
    bool m_w_scheduled = false;
    float m_w_scheduled_time = 0.0f;
    static constexpr float W_CAST_DELAY = 0.2f; // 200ms delay after AA animation starts

    // E + AA + W combo state
    bool m_e_aa_scheduled = false;
    float m_e_aa_scheduled_time = 0.0f;
    game_object* m_e_aa_target = nullptr;
    static constexpr float E_AA_DELAY = 0.1f; // 100ms after E cast starts, issue AA

    // Q + AA combo state (when E/W not available)
    bool m_q_aa_scheduled = false;
    float m_q_aa_scheduled_time = 0.0f;
    game_object* m_q_aa_target = nullptr;
    static constexpr float Q_AA_DELAY = 0.1f; // 100ms after Q cast starts, issue AA

    // Combo range threshold
    static constexpr float COMBO_RANGE_THRESHOLD = 265.0f; // 265 units for Q+E+AA+W combo
    static constexpr float SAFE_AA_BLOCK_RANGE = 150.0f; // Block orbwalker AA if within this range and have spells for animation cancel

    // Q priority state (allow AA after Q cast)
    bool m_q_just_casted = false;
    float m_q_cast_time = 0.0f;
    static constexpr float Q_AA_ALLOW_DELAY = 0.05f; // 50ms after Q cast, allow AA

    // R+E combo state (to cast Q after W in R+E+AA+W+Q combo)
    bool m_r_e_combo_active = false;
    game_object* m_r_e_combo_target = nullptr;
    float m_w_cast_time_in_combo = 0.0f; // Track when W was cast in R+E combo
    static constexpr float W_TO_Q_DELAY = 0.1f; // Delay after W cast before casting Q

    // Debug timing information
    struct debug_timing {
        float q_cast_time = 0.0f;
        float w_cast_time = 0.0f;
        float e_cast_time = 0.0f;
        float aa_time = 0.0f;
        float q_cast_duration = 0.0f;
        float w_cast_duration = 0.0f;
        float e_cast_duration = 0.0f;
        float aa_cast_delay = 0.0f;
        float aa_attack_delay = 0.0f;
    } m_debug_timing;

    // Menu options
    struct menu_options {
        // PvP - Q
        bool pvp_q_enabled = true;
        
        // PvP - W
        bool pvp_w_enabled = true;
        
        // PvP - E
        bool pvp_e_enabled = true;
        
        // PvP - R
        bool pvp_r_enabled = true;
        bool pvp_r_mode_killable = false; // false = Always, true = Killable (saved like Talon's block_r_enabled)
        bool pvp_r_only_outside_aa_range = false; // Only use R outside 380 units
        
        // PvP - Harass
        bool pvp_q_harass = true; // Use Q on harass
        bool pvp_e_harass = true; // Use E on harass
        
        // PvE
        bool pve_enabled = true;
        bool pve_q_laneclear = true; // Use Q on laneclear
        bool pve_q_lasthit = true; // Use Q on lasthit
        bool pve_w_laneclear = true; // Use W on laneclear
        bool pve_w_lasthit = true; // Use W on lasthit
        bool pve_e_fast_clear = true; // Use E on fast laneclear
        
        // Visuals
        bool visuals_enabled = true;
        bool visuals_r_damage_indicator = true; // Draw R damage on HP bars
        bool visuals_display_r_stacks = true; // Display R stack count
        bool visuals_display_r_toggle_state = true; // Display R toggle state (Always/Killable/Block)
        bool visuals_draw_q_range = true; // Draw Q range
        bool visuals_draw_e_range = true; // Draw E range
        bool visuals_draw_r_range = true; // Draw R range
        uint32_t visuals_q_range_color = 0xFFFFFFFF; // Q range color (white)
        uint32_t visuals_e_range_color = 0xFFFFFFFF; // E range color (white)
        uint32_t visuals_r_range_color = 0xFFFFFFFF; // R range color (white)
    } m_menu;
    
    // R Toggle key - cycles between Always and Killable (like Talon's block_r)
    std::string m_r_toggle_hotkey_string = "NONE";
    
    // Block R hotkey (TOGGLE) - blocks R usage and removes display
    bool m_block_r_enabled = false;
    std::string m_block_r_hotkey_string = "NONE";
    uint32_t m_block_r_infotab_id = 0;
    
    // Semi-R key (HOLD)
    bool m_semi_r_pressed = false;
    std::string m_semi_r_hotkey_string = "NONE";
    uint32_t m_semi_r_infotab_id = 0;

    // Setup functions
    void setup_spell_data();
    void init_menu(menu_category* parent_menu);
    void update_spell_data();

    // Callback wrappers
    static void __fastcall on_game_update_wrapper();
    static void __fastcall on_animation_wrapper(game_object* object, uint32_t animation_hash);
    static void __fastcall on_present_wrapper();
    static void __fastcall on_process_cast_wrapper(game_object* object, spell_cast* cast);
    static void __fastcall on_basic_attack_wrapper(game_object* object, game_object* target, spell_cast* cast);
    static bool __stdcall before_attack_wrapper(orb_sdk::event_data* data);

    // Actual callback implementations
    void on_game_update();
    void on_animation(game_object* object, uint32_t animation_hash);
    void on_present();
    void on_process_cast(game_object* object, spell_cast* cast);
    void on_basic_attack(game_object* object, game_object* target, spell_cast* cast);
    bool before_attack(orb_sdk::event_data* data);

    // Helper functions
    bool is_spell_ready(int spell_slot) const;
    bool has_enough_mana(int spell_slot) const;
    bool is_spell_learned(int spell_slot) const;
    bool is_casting() const;
    bool is_casting_except_r() const; // Check if casting, but allow R during other casts
    game_object* get_best_target(float range) const;
    
    // R logic functions
    int get_r_stack_count() const;
    float get_r_mana_cost(int stack_count) const;
    float get_r_damage(game_object* target, int stack_count) const;
    bool can_r_kill_target(game_object* target) const;

    // Combo functions
    void handle_combo();
    void handle_harass();
    bool cast_q();
    bool cast_w();
    bool cast_e();
    bool cast_r();
    bool cast_r_e_combo(game_object* target); // R+E simultaneous combo
    
    // Display functions
    void draw_r_stack_count() const;
    void draw_r_damage_indicator() const;
    void draw_r_toggle_state() const; // Display R toggle state (Always/Killable/Block)
    void draw_spell_ranges() const;
    
    // Spellfarm functions
    void handle_spellfarm();
    void use_q_spellfarm_laneclear();
    void use_q_spellfarm_lasthit();
    void use_w_spellfarm_laneclear();
    void use_w_spellfarm_lasthit();
    void use_e_fast_clear();
};
