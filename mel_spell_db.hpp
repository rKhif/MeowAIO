#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mel::spell_db
{
    enum class cast_kind : std::uint8_t
    {
        skillshot,
        targeted,
        aa,
    };

    struct entry
    {
        cast_kind kind;
        bool      reflectable; // Mel's W can bounce this back
        bool      hard_cc;     // stun / snare / charm / fear / knockup / suppression
        bool      dangerous;   // burst ult / high-priority reflect
    };

    // Keyed by lowercase internal spell name from
    // cast->get_spell_data()->get_static_data()->get_name().
    // Auto-attacks use the synthetic key "aa::<ChampName>" (champ name as
    // returned by game_object::get_char_name(), case-preserved).
    inline const std::unordered_map<std::string, entry>& table()
    {
        // Shorthand for compact entries below.
        constexpr cast_kind SS = cast_kind::skillshot;
        constexpr cast_kind TT = cast_kind::targeted;
        constexpr cast_kind AA = cast_kind::aa;
        constexpr bool      R = true,  NR = false;   // reflectable
        constexpr bool      C = true,  NC = false;   // hard_cc
        constexpr bool      D = true,  ND = false;   // dangerous

        static const std::unordered_map<std::string, entry> t = {
            // ===== HOOKS / PULLS (reflectable hard-CC skillshots) =====
            { "blitzcrankq",            { SS, R,  C,  ND } }, // Rocket Grab
            { "rocketgrab",             { SS, R,  C,  ND } },
            { "threshq",                { SS, R,  C,  ND } }, // Death Sentence
            { "threshqmissile",         { SS, R,  C,  ND } },
            { "nautilusanchordrag",     { SS, R,  C,  ND } }, // Dredge Line
            { "nautilusq",              { SS, R,  C,  ND } },
            { "pykeq",                  { SS, R,  C,  ND } }, // Bone Skewer (charged)
            { "skarnere",               { SS, R,  C,  ND } }, // Fracture
            { "camillee",               { SS, R,  C,  ND } }, // Hookshot
            { "camilleehook",           { SS, R,  C,  ND } },
            { "naafirir",               { SS, R,  NC, D  } }, // Marking projectile

            // ===== ROOTS =====
            { "morganaq",               { SS, R,  C,  ND } }, // Dark Binding
            { "darkbindingmissile",     { SS, R,  C,  ND } },
            { "luxq",                   { SS, R,  C,  ND } }, // Light Binding
            { "luxlightbinding",        { SS, R,  C,  ND } },
            { "lightbindingmissile",    { SS, R,  C,  ND } },
            { "zyrae",                  { SS, R,  C,  ND } }, // Grasping Roots
            { "neekoe",                 { SS, R,  C,  ND } }, // Tangle-Barbs
            { "varusr",                 { SS, R,  C,  D  } }, // Chain of Corruption
            { "varusrmissile",          { SS, R,  C,  D  } },
            { "ivernq",                 { SS, R,  C,  ND } }, // Rootcaller

            // ===== STUNS / CHARMS / TAUNTS via projectile =====
            { "ahrie",                  { SS, R,  C,  ND } }, // Charm
            { "ahriseduce",             { SS, R,  C,  ND } },
            { "twistedfatew",           { TT, R,  C,  ND } }, // Gold Card (targeted AA proc)
            { "goldcardpreattack",      { TT, R,  C,  ND } },
            { "lissandraq",             { SS, R,  C,  ND } }, // Ice Shard
            { "asher",                  { SS, R,  C,  D  } }, // Enchanted Crystal Arrow
            { "enchantedcrystalarrow",  { SS, R,  C,  D  } },
            { "sejuanir",               { SS, R,  C,  D  } }, // Glacial Prison
            { "sejuaniglacialprison",   { SS, R,  C,  D  } },
            { "lulutricklingmissle",    { SS, R,  NC, ND } }, // Glitterlance slow
            { "luluq",                  { SS, R,  NC, ND } },

            // ===== SLOWS / SNARES / KNOCKUPS via projectile =====
            { "namiq",                  { SS, R,  C,  ND } }, // Aqueous Accident (bubble stun)
            { "namir",                  { SS, R,  C,  D  } }, // Tidal Wave
            { "jannaq",                 { SS, R,  C,  ND } }, // Howling Gale (knockup)
            { "ornnr",                  { SS, R,  C,  D  } }, // Call of the Forge God
            { "ornnrcharge",            { SS, R,  C,  D  } },
            { "caitlynr",               { TT, R,  NC, D  } }, // Ace in the Hole
            { "caitlynaceintheholemissile", { TT, R, NC, D } },
            { "jinxr",                  { SS, R,  NC, D  } }, // Super Mega Death Rocket
            { "urgotr",                 { SS, R,  C,  D  } }, // Fear Beyond Death (pull)
            { "urgotrmissile",          { SS, R,  C,  D  } },
            { "velkozq",                { SS, R,  NC, ND } }, // Plasma Fission

            // ===== XERATH (all reflectable projectiles) =====
            { "xerathq",                { SS, R,  NC, ND } },
            { "xerathw",                { SS, R,  C,  ND } }, // Eye of Destruction (slow + stun in center)
            { "xerathmagespear",        { SS, R,  C,  ND } },
            { "xerathr",                { SS, R,  NC, D  } }, // Rite of the Arcane (global)
            { "xerathrmissile",         { SS, R,  NC, D  } },

            // ===== EZREAL R / others =====
            { "ezrealr",                { SS, R,  NC, D  } }, // Trueshot Barrage

            // ===== DANGEROUS BURST ULTS (reflectable, mostly projectile) =====
            { "rivenwindslash",         { SS, R,  NC, D  } }, // Wind Slash
            { "rivenr",                 { SS, R,  NC, D  } },
            { "veigarr",                { TT, R,  NC, D  } }, // Primordial Burst
            { "veigarprimordialburst",  { TT, R,  NC, D  } },
            { "lucianr",                { SS, R,  NC, D  } }, // The Culling
            { "thecullingmissile",      { SS, R,  NC, D  } },
            { "ambessar",               { SS, R,  NC, D  } },
            { "briarr",                 { SS, R,  NC, D  } },
            { "brandr",                 { SS, R,  NC, D  } }, // Pyroclasm
            { "syndrar",                { SS, R,  NC, D  } }, // Unleashed Power
            { "missfortuner",           { SS, R,  NC, D  } }, // Bullet Time (cone of projectiles)
            { "missfortunebullettime",  { SS, R,  NC, D  } },
            { "jhinr",                  { SS, R,  NC, D  } }, // Curtain Call
            { "jhinrshot",              { SS, R,  NC, D  } },
            { "ziggsr",                 { SS, R,  NC, D  } }, // Mega Inferno Bomb
            { "dravenr",                { SS, R,  NC, D  } }, // Whirling Death
            { "dravenrmissile",         { SS, R,  NC, D  } },
            { "hweir",                  { SS, R,  C,  D  } }, // Spiraling Despair (fear)
            { "vexr",                   { TT, R,  NC, D  } }, // Shadow Surge (homing)
            { "vexrmissile",            { TT, R,  NC, D  } },
            { "velkozr",                { SS, R,  NC, D  } }, // Life Form Disintegration Ray
            { "sonar",                  { SS, R,  C,  D  } }, // Crescendo (linear stun)

            // ===== NON-REFLECTABLE high-priority block candidates =====
            // (reflectable=false → fall under block-lethal rules only)
            { "cassiopeiar",            { SS, NR, C,  D  } }, // Petrifying Gaze (cone, not projectile)
            { "veigare",                { SS, NR, C,  ND } }, // Event Horizon (AOE field)
            { "annier",                 { TT, NR, NC, D  } }, // Summon Tibbers (point AOE)
            { "sionq",                  { SS, NR, C,  ND } }, // Decimating Smash (melee)
            { "gragase",                { SS, NR, C,  ND } }, // Body Slam (dash knockback)
            { "karthusr",               { TT, NR, NC, D  } }, // Requiem (global non-projectile)
            { "karthusfallenone",       { TT, NR, NC, D  } },
            { "amumur",                 { SS, NR, C,  D  } }, // Curse of the Sad Mummy
            { "malphiter",              { SS, NR, C,  D  } }, // Unstoppable Force (dash knockup)
            { "kennenr",                { SS, NR, C,  D  } }, // Slicing Maelstrom
            { "galiow",                 { SS, NR, C,  ND } }, // Shield of Durand (taunt)
            { "leonae",                 { SS, NR, C,  ND } }, // Zenith Blade (dash)
            { "rellw",                  { SS, NR, C,  ND } }, // Crash Down
            { "alistarw",               { SS, NR, C,  ND } }, // Headbutt (dash)
            { "mordekaiserr",           { TT, NR, NC, D  } }, // Realm of Death (targeted realm)
            { "warwickr",               { TT, NR, C,  D  } }, // Infinite Duress (suppression)
            { "anivier",                { SS, NR, NC, ND } }, // Glacial Storm (ground zone)
            { "viktorw",                { SS, NR, C,  ND } }, // Gravity Field (zone stun)
            { "cassiopeiaw",            { SS, NR, NC, ND } }, // Miasma (ground zone)
            { "vladimirr",              { TT, NR, NC, D  } }, // Hemoplague
            { "vladimirhemoplague",     { TT, NR, NC, D  } },
        };
        return t;
    }

    inline const entry* lookup(const std::string& key)
    {
        const auto& t = table();
        auto it = t.find(key);
        return it == t.end() ? nullptr : &it->second;
    }

    inline std::string make_aa_key(const std::string& char_name)
    {
        return std::string("aa::") + char_name;
    }

    inline std::string normalize_spell_name(const std::string& raw)
    {
        std::string out;
        out.reserve(raw.size());
        for (char c : raw) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            out.push_back(c);
        }
        return out;
    }
}
