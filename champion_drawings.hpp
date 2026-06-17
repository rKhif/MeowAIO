#pragma once

#include "sdk.hpp"
#include "color.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace meowaio
{

// Port of VENPlus champion_drawings::draw_config
struct draw_config
{
    static constexpr int MODE_OFF      = 0;
    static constexpr int MODE_VEN      = 1;
    static constexpr int MODE_VEN_PLUS = 2;

    int      hp_mode           = MODE_OFF;
    uint32_t hp_color          = 0x99FF4444u;
    uint32_t hp_kill_color     = 0x9900FF00u;
    bool     hp_enemies        = true;
    bool     hp_monsters       = true;
    bool     hp_minions        = false;

    bool     glow_enemies      = true;
    bool     glow_monsters     = true;
    bool     glow_minions      = false;
    uint32_t glow_color        = 0xFF00FF00u;

    void setup_menu( menu_category* parent )
    {
        if ( !parent ) return;
        parent->add_label( "Health Bar Overlay" );
        parent->add_combo( "hp_mode", "Overlay mode",
            std::vector< std::string >{ "OFF", "VEN", "MEOW" },
            hp_mode, [ this ]( int v ) { hp_mode = v; } );
        parent->add_colorpicker( "hp_color", "Damage Color", hp_color,
            [ this ]( uint32_t v ) { hp_color = v; } );
        parent->add_colorpicker( "hp_kill_color", "Killable Color", hp_kill_color,
            [ this ]( uint32_t v ) { hp_kill_color = v; } );
        parent->add_label( "Enable on:" );
        parent->add_checkbox( "hp_enemies", "Enemies", hp_enemies,
            [ this ]( bool v ) { hp_enemies = v; } );
        parent->add_checkbox( "hp_monsters", "Monsters", hp_monsters,
            [ this ]( bool v ) { hp_monsters = v; } );
        parent->add_checkbox( "hp_minions", "Minions", hp_minions,
            [ this ]( bool v ) { hp_minions = v; } );
        parent->add_separator();
        parent->add_label( "Glow Killable targets" );
        parent->add_checkbox( "glow_enemies", "Enemies", glow_enemies,
            [ this ]( bool v ) { glow_enemies = v; } );
        parent->add_checkbox( "glow_monsters", "Monsters", glow_monsters,
            [ this ]( bool v ) { glow_monsters = v; } );
        parent->add_checkbox( "glow_minions", "Minions", glow_minions,
            [ this ]( bool v ) { glow_minions = v; } );
        parent->add_colorpicker( "glow_color", "Glow Color", glow_color,
            [ this ]( uint32_t v ) { glow_color = v; } );
    }

    void draw( game_object* player, const std::function< float( game_object* ) >& get_damage ) const
    {
        if ( !player || !g_sdk || !g_sdk->object_manager || !g_sdk->renderer ) return;
        bool any_hp   = hp_mode > MODE_OFF && ( hp_enemies || hp_monsters || hp_minions );
        bool any_glow = glow_enemies || glow_monsters || glow_minions;
        if ( !any_hp && !any_glow ) return;

        if ( hp_enemies || glow_enemies )
        {
            for ( auto* h : g_sdk->object_manager->get_heroes() )
            {
                if ( !h || !h->is_valid() || h->is_dead() || !h->is_visible() ) continue;
                if ( h->get_team_id() == player->get_team_id() ) continue;

                float dmg = get_damage( h );
                if ( dmg <= 0.f ) continue;

                float ehp = h->get_hp() + h->get_all_shield();
                bool lethal = dmg >= ehp;

                if ( hp_enemies )
                {
                    if ( hp_mode == MODE_VEN )
                        g_sdk->renderer->add_damage_indicator( h, dmg );
                    else if ( hp_mode == MODE_VEN_PLUS )
                        draw_overlay( h, dmg, lethal );
                }

                if ( glow_enemies && lethal )
                    h->draw_glow( glow_color );
            }
        }

        if ( hp_monsters || glow_monsters )
        {
            for ( auto* m : g_sdk->object_manager->get_monsters() )
            {
                if ( !m || !m->is_valid() || m->is_dead() || !m->is_visible() ) continue;

                float dmg = get_damage( m );
                if ( dmg <= 0.f ) continue;

                bool lethal = dmg >= m->get_hp();

                if ( hp_monsters && hp_mode == MODE_VEN_PLUS )
                    draw_overlay( m, dmg, lethal );

                if ( glow_monsters && lethal )
                    m->draw_glow( glow_color );
            }
        }

        if ( hp_minions || glow_minions )
        {
            for ( auto* m : g_sdk->object_manager->get_minions() )
            {
                if ( !m || !m->is_valid() || m->is_dead() || !m->is_visible() ) continue;
                if ( m->get_team_id() == player->get_team_id() ) continue;
                if ( !m->is_lane_minion() && !m->is_monster() ) continue;
                if ( m->is_plant() || m->is_ward() ) continue;

                float dmg = get_damage( m );
                if ( dmg <= 0.f ) continue;

                bool lethal = dmg >= m->get_hp();

                if ( hp_minions && hp_mode == MODE_VEN_PLUS )
                    draw_overlay( m, dmg, lethal );

                if ( glow_minions && lethal )
                    m->draw_glow( glow_color );
            }
        }
    }

private:
    struct bar_rect { float ox, oy, bw, bh; };

    static constexpr bar_rect BAR_HERO    = { -45.6f, 15.5f, 106.4f, 11.5f };
    static constexpr bar_rect BAR_BARON   = { -84.f,  11.5f, 170.f,  15.f  };
    static constexpr bar_rect BAR_MONSTER = { -72.5f, 20.5f, 143.f,  10.f  };
    static constexpr bar_rect BAR_MINION  = { -31.f,  33.5f,  60.5f,  5.5f };

    static bar_rect get_bar( game_object* obj )
    {
        if ( !obj ) return BAR_MINION;

        if ( obj->is_hero() )
            return BAR_HERO;

        if ( obj->is_epic_monster() )
        {
            auto name = obj->get_char_name();
            if ( !name.empty() && name.find( "Baron" ) != std::string::npos )
                return BAR_BARON;
            return BAR_MONSTER;
        }

        if ( obj->is_large_monster() || obj->is_buff_monster() )
            return BAR_MONSTER;

        return BAR_MINION;
    }

    void draw_overlay( game_object* obj, float dmg, bool lethal ) const
    {
        if ( !obj || !obj->is_valid() ) return;
        float hp  = obj->get_hp();
        float mhp = obj->get_max_hp();
        if ( mhp <= 0.f || hp <= 0.f ) return;

        auto hbar = obj->get_health_bar_position();
        if ( hbar.x < 1.f && hbar.y < 1.f ) return;

        const float scale = g_sdk->renderer
            ? (float)g_sdk->renderer->get_window_height() / 1080.f
            : 1.f;

        auto base = get_bar( obj );
        float ox = base.ox * scale;
        float oy = base.oy * scale;
        float bw = base.bw * scale;
        float bh = base.bh * scale;

        float r0 = hp / mhp;
        float r1 = ( std::max )( 0.f, hp - dmg ) / mhp;
        if ( r0 <= r1 ) return;

        float bl = hbar.x + ox;
        float bt = hbar.y + oy;
        ::math::rect rc;
        rc.left   = bl + r1 * bw;
        rc.top    = bt;
        rc.right  = bl + r0 * bw;
        rc.bottom = bt + bh;

        g_sdk->renderer->add_rectangle_filled( rc, lethal ? hp_kill_color : hp_color );
    }
};

} // namespace meowaio
