#include "features/audio/hitsounds.hpp"

#include "features/audio/hitsound_wavs.hpp"
#include "sdk/cvar.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace shogod::features::audio {
namespace {

constexpr const char* k_sound_labels[k_sound_count] = {
    "Off",
    "Default (WAV)",
    "Switch (WAV)",
    "Killcard (WAV)",
    "Body Damage (WAV)",
    "Body Break (WAV)",
    "Wood Plank (WAV)",
    "Coin Pickup (WAV)",
    "Rank Neutral (WAV)",
    "Press (WAV)",
    "Level Up (WAV)",
    "Coin Pickup (CS2)",
    "Shop Press (CS2)",
    "Killcard (CS2)",
    "Arms Race (CS2)",
    "Claim Gift (CS2)",
    "XP Star (CS2)",
    "Tile Click (CS2)",
    "Bullet Casing (CS2)",
};

constexpr const char* k_vsnd_paths[k_vsnd_sound_count] = {
    "sounds/ui/coin_pickup_01.vsnd",
    "sounds/ui/panorama/mainmenu_press_shop_01.vsnd",
    "sounds/ui/killcard_1.vsnd",
    "sounds/ui/armsrace_level_up.vsnd",
    "sounds/ui/panorama/claim_gift_01.vsnd",
    "sounds/ui/xp_star_full_03.vsnd",
    "sounds/ui/panorama/itemtile_click_02.vsnd",
    "sounds/weapons/fx/tink/bullet_casing_07.vsnd",
};

struct queued_sound {
    int sound_index = 0;
    int volume_pct = 100;
};

struct wave_voice {
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    bool prepared = false;
    std::vector<std::int16_t> pcm;
    ~wave_voice() {
        if (device) {
            waveOutReset(device);
            if (prepared) waveOutUnprepareHeader(device, &header, sizeof header);
            waveOutClose(device);
        }
    }
};

std::atomic<std::uint64_t> g_hitmarker_ms{0};
std::atomic<bool> g_hitmarker_headshot{false};

std::mutex g_queue_mutex;
std::array<queued_sound, 32> g_queue{};
std::size_t g_queue_head = 0;
std::size_t g_queue_count = 0;

std::mutex g_voice_mutex;
std::list<std::unique_ptr<wave_voice>> g_voices;

void push_sound(int sound_index, int volume_pct) noexcept {
    if (sound_index <= 0 || sound_index >= k_sound_count || volume_pct <= 0) return;
    std::lock_guard lock(g_queue_mutex);
    if (g_queue_count >= g_queue.size()) {
        g_queue_head = (g_queue_head + 1) % g_queue.size();
        --g_queue_count;
    }
    const std::size_t idx = (g_queue_head + g_queue_count) % g_queue.size();
    g_queue[idx] = {sound_index, std::clamp(volume_pct, 0, 200)};
    ++g_queue_count;
}

void pump_finished_voices() noexcept {
    std::lock_guard lock(g_voice_mutex);
    for (auto it = g_voices.begin(); it != g_voices.end();) {
        if (((*it)->header.dwFlags & WHDR_DONE) != 0) {
            it = g_voices.erase(it);
        } else {
            ++it;
        }
    }
}

void play_embedded_wav(int wav_idx, int volume_pct) noexcept {
    if (wav_idx < 0 || wav_idx >= k_wav_sound_count || volume_pct <= 0) return;
    pump_finished_voices();

    std::lock_guard lock(g_voice_mutex);
    if (g_voices.size() >= 16) return;

    const auto& src = wavs::k_embedded_pcms[static_cast<std::size_t>(wav_idx)];
    if (!src.data || src.size < 2 || (src.size % 2) != 0) return;

    auto voice = std::make_unique<wave_voice>();
    const std::size_t samples = src.size / 2;
    voice->pcm.resize(samples);
    const int gain = std::clamp(volume_pct, 0, 200);
    for (std::size_t i = 0; i < samples; ++i) {
        const std::uint16_t lo = src.data[2 * i];
        const std::uint16_t hi = src.data[2 * i + 1];
        const auto s = static_cast<std::int16_t>(lo | (hi << 8));
        voice->pcm[i] = static_cast<std::int16_t>(std::clamp((static_cast<int>(s) * gain) / 100,
                                                             -32768, 32767));
    }

    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = 44100;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = 88200;

    if (waveOutOpen(&voice->device, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        voice->device = nullptr;
        return;
    }
    voice->header.lpData = reinterpret_cast<LPSTR>(voice->pcm.data());
    voice->header.dwBufferLength = static_cast<DWORD>(voice->pcm.size() * sizeof(std::int16_t));
    if (waveOutPrepareHeader(voice->device, &voice->header, sizeof voice->header) != MMSYSERR_NOERROR) {
        return;
    }
    voice->prepared = true;
    if (waveOutWrite(voice->device, &voice->header, sizeof voice->header) != MMSYSERR_NOERROR) {
        return;
    }
    g_voices.push_back(std::move(voice));
}

void call_play_vsnd(void* fn_ptr, const char* path) noexcept {
    if (!fn_ptr || !path) return;
    __try {
        reinterpret_cast<void(__fastcall*)(const char*)>(fn_ptr)(path);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace

const char* const* sound_names() noexcept { return k_sound_labels; }
int sound_count() noexcept { return k_sound_count; }

const char* vsnd_path_for_index(int sound_index) noexcept {
    const int rel = sound_index - k_first_vsnd_sound;
    if (rel < 0 || rel >= k_vsnd_sound_count) return nullptr;
    return k_vsnd_paths[rel];
}

sound_request select_hitsound(const config::hitsound_cfg& cfg, int hitgroup,
                              int remaining_health) noexcept {
    if (!cfg.enabled) return {};
    const bool headshot = (hitgroup == 1);
    const bool kill = (remaining_health <= 0);
    int idx = 0;
    if (headshot && kill) idx = cfg.head_kill;
    else if (headshot && !kill) idx = cfg.head_hit;
    else if (!headshot && kill) idx = cfg.body_kill;
    else idx = cfg.body_hit;
    if (idx <= 0 || idx >= k_sound_count) return {};
    const int vol_pct = std::clamp(static_cast<int>(cfg.volume * 100.0f + 0.5f), 0, 200);
    if (vol_pct <= 0) return {};
    return {idx, vol_pct};
}

void record_hit_feedback(bool enable_hitmarker, int hitgroup, int remaining_health,
                         const config::hitsound_cfg& hs_cfg) noexcept {
    if (enable_hitmarker) {
        g_hitmarker_headshot.store(hitgroup == 1, std::memory_order_relaxed);
        g_hitmarker_ms.store(GetTickCount64(), std::memory_order_release);
    }
    const sound_request req = select_hitsound(hs_cfg, hitgroup, remaining_health);
    if (req.sound_index > 0) {
        push_sound(req.sound_index, req.volume_pct);
    }
}

void reset_hitmarker() noexcept {
    g_hitmarker_ms.store(0, std::memory_order_release);
    g_hitmarker_headshot.store(false, std::memory_order_relaxed);
}

float hitmarker_alpha(std::uint64_t now_ms, bool& out_headshot) noexcept {
    const std::uint64_t t = g_hitmarker_ms.load(std::memory_order_acquire);
    out_headshot = g_hitmarker_headshot.load(std::memory_order_relaxed);
    if (t == 0 || now_ms < t) return 0.0f;
    const std::uint64_t elapsed = now_ms - t;
    if (elapsed >= 450) return 0.0f;
    return std::clamp(1.0f - static_cast<float>(elapsed) / 450.0f, 0.0f, 1.0f);
}

void preview_sound(int sound_index, float volume) noexcept {
    const int vol_pct = std::clamp(static_cast<int>(volume * 100.0f + 0.5f), 0, 200);
    if (sound_index <= 0 || sound_index >= k_sound_count || vol_pct <= 0) return;
    if (sound_index >= k_first_vsnd_sound) {
        // Queue for CS2 PlayVsnd when in-game AND fallback-play WAV #1 if offline/testing.
        push_sound(sound_index, vol_pct);
    } else {
        push_sound(sound_index, vol_pct);
    }
}

void pump_hitsounds(void* play_vsnd_ptr, std::uintptr_t cvar_iface) noexcept {
    pump_finished_voices();

    std::array<queued_sound, 8> batch{};
    std::size_t n = 0;
    {
        std::lock_guard lock(g_queue_mutex);
        while (g_queue_count > 0 && n < batch.size()) {
            batch[n++] = g_queue[g_queue_head];
            g_queue_head = (g_queue_head + 1) % g_queue.size();
            --g_queue_count;
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        const queued_sound& q = batch[i];
        if (q.sound_index >= k_first_wav_sound &&
            q.sound_index < k_first_wav_sound + k_wav_sound_count) {
            play_embedded_wav(q.sound_index - k_first_wav_sound, q.volume_pct);
        } else if (const char* vsnd = vsnd_path_for_index(q.sound_index)) {
            if (play_vsnd_ptr != nullptr) {
                if (cvar_iface != 0) {
                    sdk::write_cvar_float(cvar_iface, "snd_toolvolume",
                                          std::clamp(static_cast<float>(q.volume_pct) / 100.0f,
                                                     0.0f, 2.0f));
                }
                call_play_vsnd(play_vsnd_ptr, vsnd);
            }
        }
    }
}

}  // namespace shogod::features::audio
