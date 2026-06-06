#pragma once

#include "../pch.hpp"

namespace sora::champions
{
    struct spell_config
    {
        pred_sdk::spell_data pred{};
        int rank = 0;
        bool learned = false;

        bool is_ready(float extra_time = 0.f) const
        {
            return pred.spell_slot >= 0
                && sdk::common
                && sdk::common->spell
                && sdk::common->spell->is_spell_ready(pred.spell_slot, extra_time);
        }
    };

    class champion_base
    {
    public:
        explicit champion_base(game_object* player) : player_(player) {}
        virtual ~champion_base() = default;

        virtual void on_init() {}
        virtual void on_game_update() {}
        virtual void on_draw() {}
        virtual void combo() {}
        virtual void on_after_attack(orb_sdk::event_data*) {}
        virtual bool on_before_attack(orb_sdk::event_data*) { return true; }
        virtual void on_create_missile(game_object*) {}
        virtual void on_delete_object(game_object*) {}
        virtual void on_buff_gain(game_object*, buff_instance*) {}
        virtual void on_buff_loss(game_object*, buff_instance*) {}
        virtual void on_stop_cast(game_object*, spell_cast*, bool) {}
        virtual void on_process_cast(game_object*, spell_cast*) {}

    protected:
        game_object* player_ = nullptr;
        float game_time_ = 0.f;
        int level_ = 1;
        float hp_ = 0.f;
        float max_hp_ = 0.f;
        float hp_percent_ = 1.f;
        float mana_ = 0.f;
        float max_mana_ = 0.f;
        float mana_percent_ = 1.f;
        math::vector3 pos_{};
        float aa_range_ = 0.f;
        float ad_ = 0.f;
        float ap_ = 0.f;
        spell_config spells_[4]{};
        menu_category* menu_ = nullptr;

        void update_player_state()
        {
            player_ = g_sdk->object_manager->get_local_player();
            if (!player_)
                return;

            game_time_ = g_sdk->clock_facade->get_game_time();
            level_ = player_->get_level();
            hp_ = player_->get_hp();
            max_hp_ = player_->get_max_hp();
            hp_percent_ = max_hp_ > 0.f ? hp_ / max_hp_ : 1.f;
            mana_ = player_->get_par();
            max_mana_ = player_->get_max_par();
            mana_percent_ = max_mana_ > 0.f ? mana_ / max_mana_ : 1.f;
            pos_ = player_->get_position();
            aa_range_ = player_->get_attack_range();
            ad_ = player_->get_attack_damage();
            ap_ = player_->get_ability_power();

            for (int i = 0; i < 4; ++i)
            {
                auto* spell = player_->get_spell(i);
                if (!spell)
                    continue;

                spells_[i].rank = spell->get_level();
                spells_[i].learned = spells_[i].rank > 0;
            }
        }

        void setup_spell(int slot, pred_sdk::spell_type type, pred_sdk::targetting_type targetting)
        {
            spells_[slot].pred.spell_slot = slot;
            spells_[slot].pred.spell_type = type;
            spells_[slot].pred.targetting_type = targetting;
        }

        bool is_spell_ready(int slot, float extra_time = 0.f) const
        {
            return slot >= 0 && slot < 4 && spells_[slot].is_ready(extra_time);
        }

        bool can_cast(int slot, float cast_time = 0.25f) const
        {
            if (!is_spell_ready(slot))
                return false;
            if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack())
                return false;
            if (sdk::evade && sdk::evade->is_evading() && !sdk::evade->can_spell(slot, cast_time))
                return false;
            return true;
        }

        bool is_currently_casting() const
        {
            return sdk::common && sdk::common->spell && sdk::common->spell->is_spell_active();
        }

        bool is_attack_ready_in(float time) const
        {
            return sdk::orbwalker && sdk::orbwalker->is_attack_ready_in(time);
        }

        bool would_cancel_attack() const
        {
            return sdk::orbwalker && sdk::orbwalker->would_cancel_attack();
        }

        game_object* get_target(float range) const
        {
            if (!sdk::target_selector)
                return nullptr;

            return sdk::target_selector->get_hero_target([&](game_object* target) {
                if (!target || !target->is_valid() || target->is_dead() || !target->is_visible() || !target->is_targetable())
                    return false;
                if (player_ && target->get_team_id() == player_->get_team_id())
                    return false;
                return !player_ || target->get_position().distance(pos_) <= range + target->get_bounding_radius();
            });
        }

        game_object* get_hero_target(float range) const
        {
            return get_target(range);
        }

        game_object* get_hero_target() const
        {
            return get_target(2000.f);
        }

        bool cast_spell_pred(int slot, game_object* target, int hitchance)
        {
            if (!target || !can_cast(slot) || !sdk::prediction || !sdk::common)
                return false;

            auto data = spells_[slot].pred;
            data.expected_hitchance = hitchance;
            auto result = sdk::prediction->predict(target, data);
            if (!result.is_valid || result.hitchance < data.expected_hitchance)
                return false;

            return sdk::common->cast_spell(slot, result.cast_position);
        }

        bool cast_spell_pos(int slot, const math::vector3& pos)
        {
            return can_cast(slot) && sdk::common && sdk::common->cast_spell(slot, pos);
        }

        bool cast_spell_target(int slot, game_object* target)
        {
            return target && can_cast(slot) && sdk::common && sdk::common->cast_spell(slot, target);
        }

        bool cast_spell_self(int slot)
        {
            return can_cast(slot) && sdk::common && sdk::common->cast_spell(slot);
        }

        float predicted_hp_on_hit(int, game_object* target) const
        {
            return target ? target->get_hp() + target->get_all_shield() : FLT_MAX;
        }

        float aa_damage(game_object* target) const
        {
            if (!player_ || !target)
                return 0.f;

            return sdk::damage ? sdk::damage->get_aa_damage(player_, target) : player_->get_attack_damage();
        }

        float spell_damage(game_object* target, float raw, sora::damage::damage_type type, sora::damage::spell_kind) const
        {
            if (!player_ || !target)
                return 0.f;

            return sdk::damage ? sdk::damage->calc_damage(sora::damage::to_sdk_type(type), player_, target, raw) : raw;
        }
    };
}
