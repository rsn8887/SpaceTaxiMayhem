// audio.hpp - tiny software mixer + procedural sound effects for Space Taxi Mayhem
#pragma once
#include <SDL.h>
#include <vector>
#include <string>
#include <deque>
#include <cmath>
#include <cstdint>
#include <cstdlib>

enum Sfx {
    SFX_GEAR_DOWN, SFX_GEAR_UP, SFX_LAND, SFX_EXPLODE, SFX_PICKUP, SFX_TIP,
    SFX_FUEL, SFX_SWITCH, SFX_DOOR, SFX_TELEPORT, SFX_GATE, SFX_LEVELDONE,
    SFX_GAMEOVER, SFX_EXTRALIFE, SFX_BLIP, SFX_JOIN, SFX_LOWFUEL, SFX_LASER,
    SFX_BUMP, SFX_START, SFX_COUNT
};

enum VoiceClip { VC_HEY, VC_UP, VC_THANKS, VC_OUCH, VC_PAD1, VC_COUNT = VC_PAD1 + 9 };

struct Sample { std::vector<float> d; };

class Audio {
public:
    static const int RATE = 44100;
    static const int NCH = 24;      // 0..4 thrust loops, 5..6 voices, rest sfx
    static const int NTHRUST = 5;

    bool init(const std::string& assetDir) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        SDL_AudioSpec want{}, have{};
        want.freq = RATE; want.format = AUDIO_F32SYS; want.channels = 2;
        want.samples = 1024; want.callback = &Audio::cbStatic; want.userdata = this;
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev) { SDL_Log("audio: %s", SDL_GetError()); return false; }
        buildSfx();
        loadVoices(assetDir);
        for (int i = 0; i < NTHRUST; i++) {
            ch[i].s = &thrustLoop; ch[i].loop = true; ch[i].active = true;
            ch[i].vol = 0; ch[i].target = 0; ch[i].pos = (size_t)(i * 3001) % thrustLoop.d.size();
        }
        SDL_PauseAudioDevice(dev, 0);
        ok = true;
        return true;
    }
    void shutdown() { if (dev) SDL_CloseAudioDevice(dev); dev = 0; }

    void play(Sfx s, float vol = 1.f, float pan = 0.f) {
        if (!ok) return;
        SDL_LockAudioDevice(dev);
        int best = -1; size_t bestPos = 0;
        for (int i = 7; i < NCH; i++) {
            if (!ch[i].active) { best = i; break; }
            if (ch[i].pos > bestPos) { bestPos = ch[i].pos; best = i; }
        }
        if (best >= 0) {
            Chan& c = ch[best];
            c.s = &sfx[s]; c.pos = 0; c.vol = c.target = vol * sfxVol; c.pan = pan; c.loop = false; c.active = true;
        }
        SDL_UnlockAudioDevice(dev);
    }
    // Thrust loop volume for a player (0..1) and stereo pan (-1..1)
    void thrust(int p, float vol, float pan) {
        if (!ok || p < 0 || p >= NTHRUST) return;
        SDL_LockAudioDevice(dev);
        ch[p].target = vol * 0.55f * sfxVol; ch[p].pan = pan;
        SDL_UnlockAudioDevice(dev);
    }
    void silenceThrust() { for (int i = 0; i < NTHRUST; i++) thrust(i, 0, 0); }

    // Passenger voices are queued so they never talk over each other.
    void say(int voice, int clip, float pan = 0.f, bool droppable = false) {
        if (!ok) return;
        voice = ((voice % 3) + 3) % 3;
        if (clip < 0 || clip >= VC_COUNT || voices[voice][clip].d.empty()) return;
        SDL_LockAudioDevice(dev);
        if (droppable && (ch[5].active || !vq.empty())) { SDL_UnlockAudioDevice(dev); return; }
        if (vq.size() < 4) vq.push_back({&voices[voice][clip], pan});
        if (!ch[5].active) startNextVoice();
        SDL_UnlockAudioDevice(dev);
    }
    bool voiceBusy() {
        if (!ok) return false;
        SDL_LockAudioDevice(dev); bool b = ch[5].active || !vq.empty(); SDL_UnlockAudioDevice(dev); return b;
    }

    float sfxVol = 0.8f;
    bool ok = false;

private:
    struct Chan { const Sample* s = nullptr; size_t pos = 0; float vol = 0, target = 0, pan = 0; bool loop = false, active = false; };
    struct VQ { const Sample* s; float pan; };
    SDL_AudioDeviceID dev = 0;
    Chan ch[NCH];
    Sample sfx[SFX_COUNT];
    Sample thrustLoop;
    Sample voices[3][VC_COUNT];
    std::deque<VQ> vq;

    void startNextVoice() {
        if (vq.empty()) return;
        VQ v = vq.front(); vq.pop_front();
        ch[5].s = v.s; ch[5].pos = 0; ch[5].vol = ch[5].target = 1.0f; ch[5].pan = v.pan * 0.5f;
        ch[5].loop = false; ch[5].active = true;
    }

    static void cbStatic(void* ud, Uint8* stream, int len) { ((Audio*)ud)->cb((float*)stream, len / (int)sizeof(float) / 2); }
    void cb(float* out, int frames) {
        for (int i = 0; i < frames * 2; i++) out[i] = 0;
        for (int c = 0; c < NCH; c++) {
            Chan& h = ch[c];
            if (!h.active || !h.s || h.s->d.empty()) continue;
            const std::vector<float>& d = h.s->d;
            float lg = std::sqrt(0.5f * (1.f - h.pan)), rg = std::sqrt(0.5f * (1.f + h.pan));
            for (int i = 0; i < frames; i++) {
                if (c < NTHRUST) h.vol += (h.target - h.vol) * 0.0015f;   // smooth thrust fades
                if (h.pos >= d.size()) {
                    if (h.loop) h.pos = 0; else { h.active = false; break; }
                }
                float v = d[h.pos++] * h.vol;
                out[i * 2] += v * lg; out[i * 2 + 1] += v * rg;
            }
            if (c == 5 && !h.active) startNextVoice();
        }
        for (int i = 0; i < frames * 2; i++) {      // soft clip
            float x = out[i];
            out[i] = x / (1.f + std::fabs(x) * 0.6f);
        }
    }

    // ---------------- procedural synthesis -----------------
    static float frand() { return (float)rand() / (float)RAND_MAX * 2.f - 1.f; }
    static float sq(float ph) { return std::fmod(ph, 1.f) < 0.5f ? 1.f : -1.f; }
    static float tri(float ph) { float f = std::fmod(ph, 1.f); return f < 0.5f ? f * 4 - 1 : 3 - f * 4; }

    Sample gen(float secs, float (*fn)(float t, float& state, float& s2)) {
        Sample s; int n = (int)(secs * RATE); s.d.resize(n);
        float st = 0, st2 = 0;
        for (int i = 0; i < n; i++) s.d[i] = fn((float)i / RATE, st, st2);
        return s;
    }
    // simple helper: tone sequence (square/tri) for jingles
    Sample seq(const std::vector<std::pair<float,float>>& notes, float noteLen, int wave, float vol, float decay = 3.f) {
        Sample s; int per = (int)(noteLen * RATE);
        s.d.resize(per * notes.size() + RATE / 5);
        float ph = 0;
        for (size_t k = 0; k < notes.size(); k++) {
            float f = notes[k].first, len = notes[k].second;
            int n = (int)(per * len);
            for (int i = 0; i < n && (k * per + i) < s.d.size(); i++) {
                float t = (float)i / RATE;
                ph += f / RATE;
                float w = wave == 0 ? sq(ph) * 0.6f : (wave == 1 ? tri(ph) : (sq(ph) * 0.4f + tri(ph * 2.f) * 0.4f));
                float env = std::exp(-t * decay) * std::min(1.f, t * 300.f);
                s.d[k * per + i] += w * env * vol;
            }
        }
        return s;
    }

    void buildSfx() {
        srand(1234);
        // Thrust loop: filtered noise rumble with a bit of flutter, 1s, seamless-ish
        {
            int n = RATE; thrustLoop.d.resize(n);
            float lp = 0, lp2 = 0;
            for (int i = 0; i < n * 2; i++) {
                float x = frand();
                lp += (x - lp) * 0.18f; lp2 += (lp - lp2) * 0.25f;
                float t = (float)(i % n) / RATE;
                float fl = 0.75f + 0.25f * std::sin(t * 2 * 3.14159f * 31.f);
                if (i >= n) thrustLoop.d[i - n] = lp2 * 2.2f * fl;
            }
            // crossfade ends
            int xf = 2000;
            for (int i = 0; i < xf; i++) {
                float a = (float)i / xf;
                thrustLoop.d[i] = thrustLoop.d[i] * a + thrustLoop.d[n - xf + i] * (1 - a);
            }
            thrustLoop.d.resize(n - xf);
        }
        sfx[SFX_GEAR_DOWN] = gen(0.22f, [](float t, float& p, float&) {
            p += (420.f - t * 1200.f) / RATE;
            float click = t < 0.01f ? frand() : (t > 0.17f && t < 0.19f ? frand() * 0.9f : 0.f);
            return (sq(p) * 0.25f * (1 - t / 0.22f)) + click * 0.6f; });
        sfx[SFX_GEAR_UP] = gen(0.22f, [](float t, float& p, float&) {
            p += (180.f + t * 1300.f) / RATE;
            float click = t > 0.18f && t < 0.2f ? frand() * 0.8f : 0.f;
            return (sq(p) * 0.22f * (1 - t / 0.22f)) + click * 0.6f; });
        sfx[SFX_LAND] = gen(0.25f, [](float t, float& p, float& lp) {
            p += (110.f - t * 200.f) / RATE;
            lp += (frand() - lp) * 0.1f;
            return (std::sin(p * 6.2832f) * 0.8f + lp * 1.5f) * std::exp(-t * 18.f); });
        sfx[SFX_BUMP] = gen(0.12f, [](float t, float& p, float&) {
            p += 90.f / RATE; return std::sin(p * 6.2832f) * std::exp(-t * 30.f) * 0.7f; });
        sfx[SFX_EXPLODE] = gen(1.6f, [](float t, float& lp, float& p) {
            float cut = 0.35f * std::exp(-t * 2.2f) + 0.01f;
            lp += (frand() - lp) * cut;
            p += (60.f * std::exp(-t * 1.5f) + 25.f) / RATE;
            float boom = std::sin(p * 6.2832f) * std::exp(-t * 3.f);
            float crackle = (rand() % 100 < 3 ? frand() : 0.f) * std::exp(-t * 4.f);
            return (lp * 2.5f * std::exp(-t * 1.8f) + boom * 0.9f + crackle * 0.8f); });
        sfx[SFX_PICKUP] = seq({{660,1},{880,1},{1320,1.5f}}, 0.06f, 2, 0.45f, 8.f);
        sfx[SFX_TIP] = seq({{988,1},{1319,3}}, 0.07f, 0, 0.35f, 6.f);
        sfx[SFX_FUEL] = gen(0.07f, [](float t, float& p, float&) {
            p += (300.f + t * 3000.f) / RATE; return tri(p) * 0.3f * (1 - t / 0.07f); });
        sfx[SFX_SWITCH] = gen(0.35f, [](float t, float& p, float&) {
            float f = t < 0.12f ? 500.f : 1000.f; p += f / RATE;
            float click = t < 0.006f ? frand() : 0.f;
            return sq(p) * 0.25f * std::exp(-t * 6.f) + click; });
        sfx[SFX_DOOR] = gen(0.6f, [](float t, float& p, float& lp) {
            p += (200.f + 150.f * std::sin(t * 40.f)) / RATE;
            lp += (frand() - lp) * 0.3f;
            return (sq(p) * 0.18f + lp * 0.3f) * std::sin(3.14159f * t / 0.6f); });
        sfx[SFX_TELEPORT] = gen(0.5f, [](float t, float& p, float&) {
            p += (200.f + t * t * 8000.f) / RATE;
            return (tri(p) * 0.4f + sq(p * 1.01f) * 0.15f) * (1 - t / 0.5f); });
        sfx[SFX_GATE] = seq({{392,1},{523,1},{659,1},{784,1},{1047,3}}, 0.09f, 2, 0.4f, 3.f);
        sfx[SFX_LEVELDONE] = seq({{523,1},{659,1},{784,1},{1047,2},{784,1},{1047,4}}, 0.12f, 2, 0.4f, 2.f);
        sfx[SFX_GAMEOVER] = seq({{392,2},{370,2},{349,2},{330,6}}, 0.18f, 1, 0.5f, 1.2f);
        sfx[SFX_EXTRALIFE] = seq({{784,1},{988,1},{1175,1},{1568,1},{1175,1},{1568,3}}, 0.06f, 0, 0.3f, 4.f);
        sfx[SFX_BLIP] = seq({{880,1}}, 0.05f, 0, 0.25f, 30.f);
        sfx[SFX_JOIN] = seq({{523,1},{784,2}}, 0.07f, 2, 0.4f, 6.f);
        sfx[SFX_START] = seq({{262,1},{330,1},{392,1},{523,1},{659,1},{784,3}}, 0.07f, 2, 0.4f, 3.f);
        sfx[SFX_LOWFUEL] = seq({{1200,1},{0,1},{1200,1}}, 0.06f, 0, 0.2f, 10.f);
        sfx[SFX_LASER] = gen(0.25f, [](float t, float& p, float&) {
            p += (1600.f - t * 5000.f) / RATE; return sq(p) * 0.12f * (1 - t / 0.25f); });
    }

    bool loadClip(const std::string& path, Sample& out) {
        SDL_AudioSpec spec; Uint8* buf = nullptr; Uint32 len = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) return false;
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 1, RATE) < 0) { SDL_FreeWAV(buf); return false; }
        std::vector<Uint8> tmp(len * cvt.len_mult + 16);
        memcpy(tmp.data(), buf, len);
        cvt.buf = tmp.data(); cvt.len = (int)len;
        SDL_FreeWAV(buf);
        if (cvt.needed) SDL_ConvertAudio(&cvt); else cvt.len_cvt = cvt.len;
        int n = cvt.len_cvt / (int)sizeof(float);
        out.d.assign((float*)tmp.data(), (float*)tmp.data() + n);
        for (auto& x : out.d) x *= 0.9f;
        return true;
    }
    void loadVoices(const std::string& dir) {
        const char* names[VC_PAD1] = {"hey", "up", "thanks", "ouch"};
        int loaded = 0;
        for (int v = 0; v < 3; v++)
            for (int c = 0; c < VC_COUNT; c++) {
                std::string key = c < VC_PAD1 ? names[c] : ("pad" + std::to_string(c - VC_PAD1 + 1));
                std::string p = dir + "voice/v" + std::to_string(v) + "_" + key + ".wav";
                if (loadClip(p, voices[v][c])) loaded++;
            }
        SDL_Log("audio: %d voice clips loaded", loaded);
    }
};
