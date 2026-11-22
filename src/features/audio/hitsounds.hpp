#pragma once

// Dual-backend hitsound & hitmarker feedback engine:
// - Embedded 44.1 kHz 16-bit mono WAV PCM playback via WinMM waveOut (OverlayMenu).
// - CS2 native .vsnd playback via soundsystem.dll PlayVsnd (Reverie).
// Pure selection helpers live here for unit testing without audio hardware.

#include "config/config.hpp"

#include <cstdint>

namespace shogod::features::audio {

inline constexpr int k_sound_off = 0;
inline constexpr int k_first_wav_sound = 1;
inline constexpr int k_wav_sound_count = 10;
inline constexpr int k_first_vsnd_sound = 11;
inline constexpr int k_vsnd_sound_count = 8;
inline constexpr int k_sound_count = 19;  // 0=Off, 1..10=WAV, 11..18=CS2 vsnd

const char* const* sound_names() noexcept;
int sound_count() noexcept;
const char* vsnd_path_for_index(int sound_index) noexcept;

// Categorizes a local-attacker player_hurt event and returns the configured sound
// index (0 = muted/off) plus gain in percent (0..200).
struct sound_request {
    int sound_index = 0;
    int volume_pct = 0;
};

sound_request select_hitsound(const config::hitsound_cfg& cfg, int hitgroup,
                              int remaining_health) noexcept;

// Called from game event / hooks thread when local player damages an enemy.
void record_hit_feedback(bool enable_hitmarker, int hitgroup, int remaining_health,
                         const config::hitsound_cfg& hs_cfg) noexcept;

void reset_hitmarker() noexcept;

// Returns smoothed fade alpha in [0, 1] over 450 ms and whether last hit was headshot.
float hitmarker_alpha(std::uint64_t now_ms, bool& out_headshot) noexcept;

// Triggers an immediate test preview (from UI when user clicks a sound or Test button).
void preview_sound(int sound_index, float volume) noexcept;

// Pumps pending hitsounds: WAVs via WinMM waveOut, CS2 .vsnd via PlayVsnd when available.
using play_vsnd_fn = void (*)(const char*);
void pump_hitsounds(void* play_vsnd_ptr, std::uintptr_t cvar_iface) noexcept;

}  // namespace shogod::features::audio
