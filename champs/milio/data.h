#pragma once

namespace milio::data
{
    extern const int SLOT_Q;
    extern const int SLOT_W;
    extern const int SLOT_E;
    extern const int SLOT_R;

    extern const float Q_RANGE;
    extern const float Q_DELAY;
    extern const float Q_SPEED;
    extern const float Q_WIDTH;
    extern const float W_RANGE;
    extern const float W_RADIUS;
    extern const float W_CAST_TIME;
    extern const float E_RANGE;
    extern const float E_CAST_TIME;
    extern const float R_RADIUS;
    extern const float R_LOCKOUT;

    extern const int Q_MANA[5];
    extern const int W_MANA[5];
    extern const int E_MANA[5];
    extern const int R_MANA[3];

    extern const int Q_COOLDOWN[5];
    extern const int W_COOLDOWN[5];
    extern const int E_COOLDOWN[5];
    extern const int R_COOLDOWN[3];
}
