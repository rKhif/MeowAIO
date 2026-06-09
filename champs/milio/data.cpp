#include "data.h"

namespace milio::data
{
    const int SLOT_Q = 0;
    const int SLOT_W = 1;
    const int SLOT_E = 2;
    const int SLOT_R = 3;

    const float Q_RANGE = 1200.f;
    const float Q_DELAY = 0.25f;
    const float Q_SPEED = 1200.f;
    const float Q_WIDTH = 60.f;

    const float W_RANGE = 650.f;
    const float W_RADIUS = 415.f;
    const float W_CAST_TIME = 0.25f;

    const float E_RANGE = 650.f;
    const float E_CAST_TIME = 0.f;

    const float R_RADIUS = 700.f;
    const float R_LOCKOUT = 0.75f;

    const int Q_MANA[5] = { 50, 55, 60, 65, 70 };
    const int W_MANA[5] = { 90, 100, 110, 120, 130 };
    const int E_MANA[5] = { 50, 60, 70, 80, 90 };
    const int R_MANA[3] = { 100, 100, 100 };

    const int Q_COOLDOWN[5] = { 10, 10, 10, 10, 10 };
    const int W_COOLDOWN[5] = { 29, 27, 25, 23, 21 };
    const int E_COOLDOWN[5] = { 17, 16, 15, 14, 13 };
    const int R_COOLDOWN[3] = { 160, 145, 130 };
}
