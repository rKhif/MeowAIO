#pragma once

namespace milio::data
{
    inline constexpr int SLOT_Q = 0;
    inline constexpr int SLOT_W = 1;
    inline constexpr int SLOT_E = 2;
    inline constexpr int SLOT_R = 3;

    inline constexpr float Q_RANGE = 1200.f;
    inline constexpr float Q_DELAY = 0.25f;
    inline constexpr float Q_SPEED = 1200.f;
    inline constexpr float Q_WIDTH = 60.f;
    inline constexpr float W_RANGE = 650.f;
    inline constexpr float W_RADIUS = 415.f;
    inline constexpr float W_CAST_TIME = 0.25f;
    inline constexpr float E_RANGE = 650.f;
    inline constexpr float E_CAST_TIME = 0.f;
    inline constexpr float R_RADIUS = 700.f;
    inline constexpr float R_LOCKOUT = 0.75f;

    inline constexpr int Q_MANA[5] = { 50, 55, 60, 65, 70 };
    inline constexpr int W_MANA[5] = { 90, 100, 110, 120, 130 };
    inline constexpr int E_MANA[5] = { 50, 60, 70, 80, 90 };
    inline constexpr int R_MANA[3] = { 100, 100, 100 };

    inline constexpr int Q_COOLDOWN[5] = { 10, 10, 10, 10, 10 };
    inline constexpr int W_COOLDOWN[5] = { 29, 27, 25, 23, 21 };
    inline constexpr int E_COOLDOWN[5] = { 17, 16, 15, 14, 13 };
    inline constexpr int R_COOLDOWN[3] = { 160, 145, 130 };
}
