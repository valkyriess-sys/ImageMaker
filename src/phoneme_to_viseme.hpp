#pragma once
// ─── ImageMaker M8: Lip-Sync System ──────────────────────────────────
// Text → Phoneme → Viseme mouth shape animation
// Builds on M7 expression template system (BlendDelta, ExprConfig, etc.)
//
// Pipeline:
//   1. Input text string
//   2. Grapheme-to-phoneme conversion (rule-based English)
//   3. Phoneme → viseme index (16-viseme mapping)
//   4. Estimated timing (each phoneme ~80ms, word boundaries +pause)
//   5. Runtime: advance clock, interpolate viseme, apply delta to face base
//
// Usage in main loop:
//   if (lipSyncPlaying) {
//       lipSyncTime += deltaTime;
//       int vi = lipSyncTrack.sample(lipSyncTime);
//       if (vi >= 0) applyViseme(vi, 1.0f);
//       if (lipSyncTime >= lipSyncTrack.totalDuration) lipSyncPlaying = false;
//   }

#include "expression_template.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <unordered_map>

// ─── M8: Viseme index (standard 16-viseme set, phoneme-grouped) ──────
// Compatible with common facial animation pipelines (Unreal, Oculus Lipsync, etc.)
enum Viseme : int {
    VISEME_REST      = 0,   // silence, rest position
    VISEME_AA        = 1,   // AA, AH, AO, AW, AX  (open back)
    VISEME_AE        = 2,   // AE, EH              (open front)
    VISEME_EY        = 3,   // EY, AY              (wide diphthong)
    VISEME_IY        = 4,   // IY, IH              (close front, wide)
    VISEME_OW        = 5,   // OW, OY, W           (rounded mid)
    VISEME_UW        = 6,   // UW, UH              (rounded close)
    VISEME_BMP       = 7,   // B, M, P             (bilabial closed)
    VISEME_FV        = 8,   // F, V                (labiodental)
    VISEME_THDH      = 9,   // TH, DH              (dental)
    VISEME_TDSNL     = 10,  // T, D, S, Z, N, L    (alveolar)
    VISEME_CHJHSH    = 11,  // CH, JH, SH, ZH      (postalveolar)
    VISEME_KGNG      = 12,  // K, G, NG            (velar)
    VISEME_R         = 13,  // R                    (retroflex/r-colored)
    VISEME_ER        = 14,  // ER, UR, IR          (rhotic vowel)
    VISEME_COUNT     = 16
};

// ─── Grapheme-to-Phoneme: simplified English rule-based converter ──────
// Maps character sequences to phoneme labels (ARPAbet-style).
// Supports common English letter-to-sound rules.
namespace GraphemeToPhoneme {

struct PhonemeRule {
    const char* pattern;   // character sequence to match (lowercase)
    int matchLen;          // length of match
    const char* phoneme;   // output phoneme label
};

// Phoneme lookup: simple longest-match-first rules for English
inline bool matchAt(const char* text, int pos, int len, const char* pattern, int plen) {
    if (pos + plen > len) return false;
    for (int i = 0; i < plen; i++) {
        if (tolower((unsigned char)text[pos + i]) != pattern[i]) return false;
    }
    return true;
}

// Extract phonemes from text with timing estimates
// Uses simplified rules. Real apps would use espeak/lib or CMU dict.
struct PhonemeTiming {
    std::string phoneme;   // phoneme label (e.g. "AA", "B", "CH")
    float startTime;       // seconds from start
    float duration;        // seconds
};

inline std::vector<PhonemeTiming> textToPhonemes(const std::string& text) {
    std::vector<PhonemeTiming> result;
    
    // Simplified phoneme rules — longest match first
    // Multi-char patterns checked before single chars
    struct Rule { const char* seq; int len; const char* ph; };
    static const Rule rules[] = {
        {"ch", 2, "CH"}, {"sh", 2, "SH"}, {"th", 2, "TH"}, {"ng", 2, "NG"},
        {"wh", 2, "WH"}, {"ph", 2, "F"},  {"ck", 2, "K"},  {"gh", 2, "G"},
        {"ee", 2, "IY"}, {"ea", 2, "IY"}, {"oo", 2, "UW"}, {"ou", 2, "AW"},
        {"ow", 2, "OW"}, {"oy", 2, "OY"}, {"ai", 2, "EY"}, {"ay", 2, "EY"},
        {"aw", 2, "AA"}, {"au", 2, "AO"}, {"oi", 2, "OY"}, {"ew", 2, "UW"},
        {"ar", 2, "AA"}, {"er", 2, "ER"}, {"ir", 2, "ER"}, {"ur", 2, "ER"},
        {"or", 2, "AO"}, {"qu", 2, "KW"},
    };
    
    int len = (int)text.size();
    int pos = 0;
    float time = 0.0f;
    float phonemeDuration = 0.07f; // ~70ms per phoneme (adjustable)
    
    while (pos < len) {
        char c = (char)tolower((unsigned char)text[pos]);
        
        // Word boundary → silent pause
        if (c == ' ' || c == '\t' || c == '\n') {
            time += 0.15f; // pause between words
            pos++;
            continue;
        }
        
        // Punctuation → brief pause (no phoneme)
        if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') {
            time += 0.3f; // sentence boundary
            pos++;
            continue;
        }
        
        // Try multi-char rules first (longest match)
        bool matched = false;
        for (auto& r : rules) {
            if (matchAt(text.c_str(), pos, len, r.seq, r.len)) {
                PhonemeTiming pt;
                pt.phoneme = r.ph;
                pt.startTime = time;
                pt.duration = phonemeDuration;
                result.push_back(pt);
                time += phonemeDuration;
                pos += r.len;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        
        // Single character rules
        const char* ph = nullptr;
        switch (c) {
            case 'a': ph = "AE"; break;
            case 'b': ph = "B";  break;
            case 'c': // context-dependent: before e/i/y → S, otherwise K
                if (pos + 1 < len) {
                    char nc = tolower((unsigned char)text[pos + 1]);
                    if (nc == 'e' || nc == 'i' || nc == 'y') ph = "S";
                    else ph = "K";
                } else ph = "K";
                break;
            case 'd': ph = "D";  break;
            case 'e': ph = "EH"; break;
            case 'f': ph = "F";  break;
            case 'g': 
                if (pos + 1 < len) {
                    char nc = tolower((unsigned char)text[pos + 1]);
                    if (nc == 'e' || nc == 'i' || nc == 'y') ph = "JH";
                    else ph = "G";
                } else ph = "G";
                break;
            case 'h': ph = "HH"; break;
            case 'i': ph = "IH"; break;
            case 'j': ph = "JH"; break;
            case 'k': ph = "K";  break;
            case 'l': ph = "L";  break;
            case 'm': ph = "M";  break;
            case 'n': ph = "N";  break;
            case 'o': ph = "OW"; break;
            case 'p': ph = "P";  break;
            case 'q': ph = "K";  pos++; /* skip u in qu */ break; // handled above
            case 'r': ph = "R";  break;
            case 's': ph = "S";  break;
            case 't': ph = "T";  break;
            case 'u': ph = "AH"; break;
            case 'v': ph = "V";  break;
            case 'w': ph = "W";  break;
            case 'x': ph = "KS"; break; // simplified
            case 'y': ph = "Y";  break;
            case 'z': ph = "Z";  break;
            default: 
                pos++;
                continue; // skip unrecognized
        }
        
        if (ph) {
            PhonemeTiming pt;
            pt.phoneme = ph;
            pt.startTime = time;
            pt.duration = phonemeDuration;
            result.push_back(pt);
            time += phonemeDuration;
        }
        pos++;
    }
    
    return result;
}

} // namespace GraphemeToPhoneme

// ─── Phoneme → Viseme Mapping ────────────────────────────────────────
// Standard phoneme groups mapped to viseme indices
inline int phonemeToViseme(const std::string& phoneme) {
    static const std::unordered_map<std::string, int> map = {
        // Silence / rest
        {"SIL", VISEME_REST}, {"SP", VISEME_REST},
        
        // AA group — open back vowels
        {"AA", VISEME_AA}, {"AH", VISEME_AA}, {"AO", VISEME_AA},
        {"AW", VISEME_AA}, {"AX", VISEME_AA},
        
        // AE group — open front vowels
        {"AE", VISEME_AE}, {"EH", VISEME_AE},
        
        // EY group — wide diphthong / mid-front
        {"EY", VISEME_EY}, {"AY", VISEME_EY},
        
        // IY group — close front
        {"IY", VISEME_IY}, {"IH", VISEME_IY},
        
        // OW group — rounded mid
        {"OW", VISEME_OW}, {"OY", VISEME_OW}, {"W",  VISEME_OW},
        
        // UW group — rounded close
        {"UW", VISEME_UW}, {"UH", VISEME_UW},
        
        // Bilabial
        {"B", VISEME_BMP}, {"M", VISEME_BMP}, {"P", VISEME_BMP},
        
        // Labiodental
        {"F", VISEME_FV}, {"V", VISEME_FV},
        
        // Dental
        {"TH", VISEME_THDH}, {"DH", VISEME_THDH},
        
        // Alveolar
        {"T", VISEME_TDSNL}, {"D", VISEME_TDSNL}, {"S", VISEME_TDSNL},
        {"Z", VISEME_TDSNL}, {"N", VISEME_TDSNL}, {"L", VISEME_TDSNL},
        
        // Postalveolar
        {"CH", VISEME_CHJHSH}, {"JH", VISEME_CHJHSH},
        {"SH", VISEME_CHJHSH}, {"ZH", VISEME_CHJHSH},
        
        // Velar
        {"K", VISEME_KGNG}, {"G", VISEME_KGNG}, {"NG", VISEME_KGNG},
        
        // Rhotic
        {"R", VISEME_R},
        {"ER", VISEME_ER}, {"UR", VISEME_ER}, {"IR", VISEME_ER},
        
        // Glottal / aspirated (near rest)
        {"HH", VISEME_REST}, {"WH", VISEME_REST},
        
        // Additional
        {"Y", VISEME_IY}, {"KW", VISEME_UW}, {"KS", VISEME_TDSNL},
    };
    
    auto it = map.find(phoneme);
    return (it != map.end()) ? it->second : VISEME_REST;
}

// ─── Lip-Sync Timing Key ─────────────────────────────────────────────
struct LipSyncKey {
    float time;     // seconds from start
    int viseme;     // VISEME_* index
};

// ─── Lip-Sync Animation Track ────────────────────────────────────────
struct LipSyncTrack {
    std::vector<LipSyncKey> keys;
    float totalDuration = 0.0f;
    std::string transcript; // original text
    
    // Build from text string
    static LipSyncTrack build(const std::string& text) {
        LipSyncTrack track;
        track.transcript = text;
        
        auto pts = GraphemeToPhoneme::textToPhonemes(text);
        
        for (auto& pt : pts) {
            LipSyncKey key;
            key.time = pt.startTime;
            key.viseme = phonemeToViseme(pt.phoneme);
            track.keys.push_back(key);
            
            // Also add key at end of phoneme for interpolation
            LipSyncKey endKey;
            endKey.time = pt.startTime + pt.duration;
            endKey.viseme = key.viseme;
            track.keys.push_back(endKey);
        }
        
        // Add trailing rest
        if (!pts.empty()) {
            track.totalDuration = pts.back().startTime + pts.back().duration;
            LipSyncKey rest;
            rest.time = track.totalDuration;
            rest.viseme = VISEME_REST;
            track.keys.push_back(rest);
        }
        
        return track;
    }
    
    // Find current viseme at time t (linear interpolation with neighbor)
    int sample(float t) const {
        if (keys.empty()) return VISEME_REST;
        if (t >= totalDuration) return VISEME_REST;
        if (t <= keys[0].time) return keys[0].viseme;
        
        for (size_t i = 0; i + 1 < keys.size(); i++) {
            if (t >= keys[i].time && t < keys[i + 1].time) {
                return keys[i].viseme;
            }
        }
        return keys.back().viseme;
    }
    
    // Get fractional blend between visemes (for smooth interpolation)
    // Returns current viseme and blend weight toward next
    int sampleInterp(float t, float blendWindow, float& weight) const {
        weight = 0.0f;
        if (keys.empty()) return VISEME_REST;
        if (t >= totalDuration) return VISEME_REST;
        if (t <= keys[0].time) return keys[0].viseme;
        
        for (size_t i = 0; i + 1 < keys.size(); i++) {
            if (t >= keys[i].time && t < keys[i + 1].time) {
                float segment = keys[i + 1].time - keys[i].time;
                if (segment > 0.001f) {
                    float frac = (t - keys[i].time) / segment;
                    if (frac < blendWindow) {
                        // Blend from previous to current
                        int prev = (i > 0) ? keys[i - 1].viseme : keys[i].viseme;
                        weight = 1.0f - (frac / blendWindow);
                        // Return prev as base, apply current with decreasing weight
                        // This is a direction indicator; caller handles
                    }
                    if (frac > (1.0f - blendWindow) && i + 2 < keys.size()) {
                        weight = (frac - (1.0f - blendWindow)) / blendWindow;
                        return keys[i + 1].viseme;
                    }
                }
                return keys[i].viseme;
            }
        }
        return keys.back().viseme;
    }
    
    void clear() {
        keys.clear();
        totalDuration = 0.0f;
        transcript.clear();
    }
};

// ─── Viseme Configuration ─────────────────────────────────────────────
// Each viseme defined as mouth-only ExprConfig for BlendDelta generation.
// These map to the same ExprConfig parameters used by M7's generateExpression().
namespace VisemeConfig {

// Generate all 16 viseme BlendDeltas from a face base mesh
inline void generateAllVisemes(const Mesh& faceBase,
                                BlendDelta outDeltas[VISEME_COUNT]) {
    using ETS = ExpressionTemplateSystem;
    
    // Each viseme is an ExprConfig with mouth parameters only
    // mouth_open: 0=closed, 1=wide open
    // mouth_stretch: positive=wide, negative=narrow/rounded
    // jaw_drop: 0=normal, 1=dropped
    // mouth_corner_up: positive=smile, negative=frown
    
    struct VisemeParams {
        float mouth_open;
        float mouth_stretch;
        float jaw_drop;
        float mouth_corner_up;  // subtle lip shaping
    };
    
    static const VisemeParams params[VISEME_COUNT] = {
        // 0: REST — relaxed neutral mouth
        { 0.0f,   0.0f,   0.0f,   0.0f },
        // 1: AA — open back, relaxed wide (AH, AA, AW)
        { 0.5f,   0.1f,   0.3f,   0.0f },
        // 2: AE — open front, slightly wider (AE, EH)
        { 0.4f,   0.25f,  0.25f,  0.0f },
        // 3: EY — mid-close, wide stretched (EY, AY)
        { 0.2f,   0.6f,   0.1f,   0.15f },
        // 4: IY — close front, very wide smile (IY, IH, Y)
        { 0.1f,   0.7f,   0.05f,  0.25f },
        // 5: OW — rounded mid, pursed (OW, OY, W)
        { 0.3f,   -0.5f,  0.1f,   0.0f },
        // 6: UW — rounded close, very pursed (UW, UH)
        { 0.05f,  -0.65f, 0.02f,  0.0f },
        // 7: BMP — bilabial closed (B, M, P)
        { -0.02f, 0.0f,   0.0f,   0.0f },
        // 8: FV — labiodental, lower lip under upper teeth (F, V)
        { 0.02f,  -0.1f,  0.02f,  0.0f },
        // 9: THDH — dental, tongue tip between teeth (TH, DH)
        { 0.05f,  0.05f,  0.03f,  0.0f },
        // 10: TDSNL — alveolar, slightly open (T, D, S, Z, N, L)
        { 0.08f,  0.0f,   0.05f,  0.0f },
        // 11: CHJHSH — postalveolar, rounded and open (CH, JH, SH, ZH)
        { 0.15f,  -0.25f, 0.08f,  0.0f },
        // 12: KGNG — velar, back of mouth (K, G, NG)
        { 0.1f,   0.0f,   0.08f,  0.0f },
        // 13: R — retroflex, tongue curled (R)
        { 0.08f,  -0.15f, 0.04f,  0.0f },
        // 14: ER — rhotic vowel, slightly open rounded (ER, UR, IR)
        { 0.25f,  -0.2f,  0.12f,  0.0f },
        // 15: reserve — additional viseme (unused)
        { 0.0f,   0.0f,   0.0f,   0.0f },
    };
    
    for (int v = 0; v < VISEME_COUNT; v++) {
        const auto& p = params[v];
        ExpressionTemplateSystem::ExprConfig cfg{};
        cfg.eye_close = 0.0f;
        cfg.brow_raise = 0.0f;
        cfg.brow_inner_up = 0.0f;
        cfg.mouth_corner_up = p.mouth_corner_up;
        cfg.mouth_open = p.mouth_open;
        cfg.mouth_stretch = p.mouth_stretch;
        cfg.cheek_raise = 0.0f;
        cfg.nose_wrinkle = 0.0f;
        cfg.jaw_drop = p.jaw_drop;
        
        char name[32];
        snprintf(name, sizeof(name), "viseme_%d", v);
        outDeltas[v] = ETS::generateExpression(faceBase, cfg, name);
    }
}

// Names for debug display
inline const char* visemeName(int v) {
    static const char* names[] = {
        "rest", "AA", "AE", "EY", "IY", "OW", "UW",
        "BMP", "FV", "THDH", "TDSNL", "CHJHSH", "KGNG", "R", "ER", "resv"
    };
    if (v >= 0 && v < VISEME_COUNT) return names[v];
    return "?";
}

} // namespace VisemeConfig

// ─── Convenience: build LipSync pipeline from text ────────────────────
// Returns a track ready for playback
inline LipSyncTrack buildLipSync(const std::string& text) {
    return LipSyncTrack::build(text);
}
