#pragma once

// All math is 2D (XZ plane) — Y is height, preserved but not used in distance/angle calculations

// Use the SDK's canonical math types. A plain "math.hpp" resolves to the
// duplicate MeowV1/math.hpp from this include context and redefines vector2,
// vector3, vector4, rect, cmpf, deg2rad, and rad2deg in the same translation unit.
#include "../../math.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace venplus::math
{

inline constexpr float PI      = 3.14159265358979323846f;
inline constexpr float TWO_PI  = 6.28318530717958647692f;
inline constexpr float HALF_PI = 1.57079632679489661923f;
inline constexpr float EPSILON = 0.001f;

inline float dist( const ::math::vector3& a, const ::math::vector3& b ) noexcept
{
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return std::sqrt( dx * dx + dz * dz );
}

inline float dist_sq( const ::math::vector3& a, const ::math::vector3& b ) noexcept
{
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

inline float length( const ::math::vector3& v ) noexcept
{
    return std::sqrt( v.x * v.x + v.z * v.z );
}

inline float length_sq( const ::math::vector3& v ) noexcept
{
    return v.x * v.x + v.z * v.z;
}

inline ::math::vector3 normalize( const ::math::vector3& v ) noexcept
{
    float len = length( v );
    if ( len < EPSILON ) return { 0.f, 0.f, 0.f };
    return { v.x / len, 0.f, v.z / len };
}

inline ::math::vector3 direction( const ::math::vector3& from, const ::math::vector3& to ) noexcept
{
    return normalize( { to.x - from.x, 0.f, to.z - from.z } );
}

inline ::math::vector3 extend( const ::math::vector3& from, const ::math::vector3& to, float distance ) noexcept
{
    if ( dist_sq( from, to ) < EPSILON * EPSILON )
        return from;

    auto dir = direction( from, to );
    return { from.x + dir.x * distance, from.y, from.z + dir.z * distance };
}

inline ::math::vector3 lerp( const ::math::vector3& a, const ::math::vector3& b, float t ) noexcept
{
    return {
        a.x + ( b.x - a.x ) * t,
        a.y + ( b.y - a.y ) * t,
        a.z + ( b.z - a.z ) * t,
    };
}

inline ::math::vector3 perpendicular( const ::math::vector3& dir ) noexcept
{
    return { -dir.z, 0.f, dir.x };
}

inline ::math::vector3 rotate( const ::math::vector3& point, const ::math::vector3& origin, float angle ) noexcept
{
    float s = std::sin( angle );
    float c = std::cos( angle );
    float dx = point.x - origin.x;
    float dz = point.z - origin.z;
    return {
        origin.x + dx * c - dz * s,
        point.y,
        origin.z + dx * s + dz * c,
    };
}

inline float angle_between( const ::math::vector3& a, const ::math::vector3& b ) noexcept
{
    float dot = a.x * b.x + a.z * b.z;
    float la = length( a );
    float lb = length( b );
    if ( la < EPSILON || lb < EPSILON ) return 0.f;
    float cos_val = std::clamp( dot / ( la * lb ), -1.f, 1.f );
    return std::acos( cos_val );
}

inline float dot2d( const ::math::vector3& a, const ::math::vector3& b ) noexcept
{
    return a.x * b.x + a.z * b.z;
}

inline float cross2d( const ::math::vector3& a, const ::math::vector3& b ) noexcept
{
    return a.x * b.z - a.z * b.x;
}

inline ::math::vector3 closest_point_on_segment(
    const ::math::vector3& p,
    const ::math::vector3& a,
    const ::math::vector3& b ) noexcept
{
    float dx = b.x - a.x;
    float dz = b.z - a.z;
    float len_sq = dx * dx + dz * dz;

    if ( len_sq < EPSILON )
        return a;

    float t = std::clamp(
        ( ( p.x - a.x ) * dx + ( p.z - a.z ) * dz ) / len_sq,
        0.f, 1.f
    );

    return { a.x + t * dx, a.y, a.z + t * dz };
}

inline float dist_to_segment(
    const ::math::vector3& p,
    const ::math::vector3& a,
    const ::math::vector3& b ) noexcept
{
    auto closest = closest_point_on_segment( p, a, b );
    return dist( p, closest );
}

inline bool point_in_rect(
    const ::math::vector3& p,
    const ::math::vector3& start,
    const ::math::vector3& end,
    float width ) noexcept
{
    return dist_to_segment( p, start, end ) <= width;
}

inline bool point_in_circle(
    const ::math::vector3& p,
    const ::math::vector3& center,
    float radius ) noexcept
{
    return dist_sq( p, center ) <= radius * radius;
}

inline bool point_in_cone(
    const ::math::vector3& p,
    const ::math::vector3& origin,
    const ::math::vector3& dir,
    float half_angle,
    float range ) noexcept
{
    float d = dist( origin, p );
    if ( d > range ) return false;
    if ( d < EPSILON ) return true;

    auto to_p = direction( origin, p );
    float angle = angle_between( dir, to_p );
    return angle <= half_angle;
}

struct line_circle_result
{
    bool  hit     = false;
    float t1      = 0.f;
    float t2      = 0.f;
    int   count   = 0;
};

inline line_circle_result line_circle_intersect(
    const ::math::vector3& a,
    const ::math::vector3& b,
    const ::math::vector3& center,
    float radius ) noexcept
{
    line_circle_result result;

    float dx = b.x - a.x;
    float dz = b.z - a.z;
    float aa = dx * dx + dz * dz;

    if ( aa < EPSILON )
    {
        float fx = a.x - center.x;
        float fz = a.z - center.z;
        result.hit = ( fx * fx + fz * fz ) <= radius * radius;
        return result;
    }

    float fx = a.x - center.x;
    float fz = a.z - center.z;

    float bb = 2.f * ( fx * dx + fz * dz );
    float cc = fx * fx + fz * fz - radius * radius;

    float disc = bb * bb - 4.f * aa * cc;

    if ( disc < 0.f )
        return result;

    disc = std::sqrt( disc );

    result.t1 = ( -bb - disc ) / ( 2.f * aa );
    result.t2 = ( -bb + disc ) / ( 2.f * aa );

    result.count = 0;
    if ( result.t1 >= 0.f && result.t1 <= 1.f ) result.count++;
    if ( result.t2 >= 0.f && result.t2 <= 1.f ) result.count++;

    result.hit = result.count > 0 ||
                 ( result.t1 < 0.f && result.t2 > 1.f );

    return result;
}

inline bool segment_intersect(
    const ::math::vector3& a1, const ::math::vector3& a2,
    const ::math::vector3& b1, const ::math::vector3& b2 ) noexcept
{
    float d1x = a2.x - a1.x, d1z = a2.z - a1.z;
    float d2x = b2.x - b1.x, d2z = b2.z - b1.z;

    float denom = d1x * d2z - d1z * d2x;
    if ( std::abs( denom ) < EPSILON ) return false;

    float t = ( ( b1.x - a1.x ) * d2z - ( b1.z - a1.z ) * d2x ) / denom;
    float u = ( ( b1.x - a1.x ) * d1z - ( b1.z - a1.z ) * d1x ) / denom;

    return t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f;
}

// speed <= 0 or FLT_MAX = instant (no projectile)
inline float travel_time( float distance, float speed, float delay = 0.f ) noexcept
{
    if ( speed <= 0.f || speed >= std::numeric_limits< float >::max() * 0.5f )
        return delay;
    return delay + distance / speed;
}

} // namespace venplus::math
