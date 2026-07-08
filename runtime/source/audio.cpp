#include "gml_runtime.h"
#include "engine_internal.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels,
                                        int* sample_rate, short** output);

namespace gml {

static const int kStreamBase = 200000;
static const int kHandleBase = 300000;

static ma_engine g_engine;
static bool g_engine_ok = false;
static bool g_engine_tried = false;

static std::vector<std::string> g_streams;

int kwik_stream_register(const std::string& path) {
    g_streams.push_back(path);
    return kStreamBase + (int)g_streams.size() - 1;
}

const std::string* kwik_stream_path(int id) {
    int i = id - kStreamBase;
    if (i < 0 || (size_t)i >= g_streams.size()) return nullptr;
    return &g_streams[i];
}

struct Voice {
    ma_sound snd;
    ma_audio_buffer* buffer = nullptr;
    short* pcm = nullptr;
    int asset = -1;
    int stream = -1;
    bool active = false;
    bool paused = false;
    float base_gain = 1.0f;
    float gain = 1.0f;
    float fade_target = -1.0f;
    float fade_step = 0.0f;
};

static std::vector<Voice*> g_voices;

static bool ensure_engine() {
    if (g_engine_tried) return g_engine_ok;
    g_engine_tried = true;
    g_engine_ok = ma_engine_init(nullptr, &g_engine) == MA_SUCCESS;
    if (!g_engine_ok) std::fprintf(stderr, "[audio] engine init failed\n");
    return g_engine_ok;
}

static void free_voice(Voice* v) {
    ma_sound_stop(&v->snd);
    ma_sound_uninit(&v->snd);
    if (v->buffer) {
        ma_audio_buffer_uninit(v->buffer);
        delete v->buffer;
    }
    if (v->pcm) std::free(v->pcm);
    delete v;
}

static bool finish_voice_from_pcm(Voice* v, short* pcm, ma_uint32 channels, ma_uint32 rate,
                                  ma_uint64 frames) {
    v->pcm = pcm;
    v->buffer = new ma_audio_buffer();
    ma_audio_buffer_config cfg =
        ma_audio_buffer_config_init(ma_format_s16, channels, frames, pcm, nullptr);
    cfg.sampleRate = rate;
    if (ma_audio_buffer_init(&cfg, v->buffer) != MA_SUCCESS) {
        delete v->buffer;
        v->buffer = nullptr;
        return false;
    }
    return ma_sound_init_from_data_source(&g_engine, v->buffer, 0, nullptr, &v->snd) == MA_SUCCESS;
}

static bool init_voice_pcm(Voice* v, const unsigned char* data, unsigned int size, int type) {
    if (type == 2 || (size >= 4 && !std::memcmp(data, "OggS", 4))) {
        int channels = 0, sample_rate = 0;
        short* pcm = nullptr;
        int frames = stb_vorbis_decode_memory(data, (int)size, &channels, &sample_rate, &pcm);
        if (frames <= 0 || !pcm) return false;
        return finish_voice_from_pcm(v, pcm, (ma_uint32)channels, (ma_uint32)sample_rate,
                                     (ma_uint64)frames);
    }
    ma_decoder dec;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);
    if (ma_decoder_init_memory(data, size, &cfg, &dec) != MA_SUCCESS) return false;
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &frames) != MA_SUCCESS || frames == 0) {
        ma_decoder_uninit(&dec);
        return false;
    }
    ma_uint32 channels = dec.outputChannels;
    ma_uint32 rate = dec.outputSampleRate;
    short* pcm = (short*)std::malloc((size_t)frames * channels * sizeof(short));
    if (!pcm) {
        ma_decoder_uninit(&dec);
        return false;
    }
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&dec, pcm, frames, &got);
    ma_decoder_uninit(&dec);
    if (got == 0) {
        std::free(pcm);
        return false;
    }
    return finish_voice_from_pcm(v, pcm, channels, rate, got);
}

static bool read_file_bytes(const std::string& path, std::vector<unsigned char>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize(n);
    size_t got = std::fread(out.data(), 1, n, f);
    std::fclose(f);
    out.resize(got);
    return !out.empty();
}

static Voice* start_voice(int what, bool loop) {
    if (!ensure_engine()) return nullptr;

    Voice* v = new Voice();
    bool ok = false;
    float vol = 1.0f, pitch = 1.0f;

    if (what >= kStreamBase && what < kHandleBase) {
        const std::string* path = kwik_stream_path(what);
        std::vector<unsigned char> bytes;
        if (path) {
            if (!read_file_bytes(*path, bytes)) {
                size_t slash = path->find_last_of('/');
                std::string base = slash == std::string::npos ? *path : path->substr(slash + 1);
                if (!read_file_bytes("mus/" + base, bytes))
                    if (!read_file_bytes("../mus/" + base, bytes)) read_file_bytes(base, bytes);
            }
            if (!bytes.empty())
                ok = init_voice_pcm(v, bytes.data(), (unsigned)bytes.size(), 0);
            else
                std::fprintf(stderr, "[audio] stream not found: %s\n", path->c_str());
        }
        v->stream = what;
    } else if (what >= 0 && what < g_sound_count) {
        const KwikSound& s = g_sound_table[what];
        vol = s.volume > 0 ? s.volume : 1.0f;
        pitch = s.pitch > 0 ? s.pitch : 1.0f;
        if (s.blob >= 0) {
            unsigned int size = 0;
            int type = 0;
            const unsigned char* data = kwik_sound_blob(s.blob, size, type);
            if (data && size) ok = init_voice_pcm(v, data, size, type);
        } else if (s.file && *s.file) {
            std::vector<unsigned char> bytes;
            std::string fn = s.file;
            if (!read_file_bytes(fn, bytes)) {
                size_t dot = fn.find_last_of('.');
                std::string base = dot == std::string::npos ? fn : fn.substr(0, dot);
                if (!read_file_bytes(base + ".ogg", bytes)) read_file_bytes(base + ".wav", bytes);
            }
            if (!bytes.empty()) ok = init_voice_pcm(v, bytes.data(), (unsigned)bytes.size(), 0);
        }
        v->asset = what;
    }

    if (!ok) {
        if (v->buffer) {
            ma_audio_buffer_uninit(v->buffer);
            delete v->buffer;
        }
        if (v->pcm) std::free(v->pcm);
        delete v;
        return nullptr;
    }
    v->base_gain = vol;
    v->gain = 1.0f;
    ma_sound_set_volume(&v->snd, vol);
    ma_sound_set_pitch(&v->snd, pitch);
    ma_sound_set_looping(&v->snd, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&v->snd);
    v->active = true;
    return v;
}

static int store_voice(Voice* v) {
    for (size_t i = 0; i < g_voices.size(); ++i) {
        if (!g_voices[i]) {
            g_voices[i] = v;
            return (int)i;
        }
    }
    g_voices.push_back(v);
    return (int)g_voices.size() - 1;
}

static Voice* voice_of_handle(int h) {
    int i = h - kHandleBase;
    if (i < 0 || (size_t)i >= g_voices.size()) return nullptr;
    return g_voices[i];
}

template <typename F>
static void for_matching(int what, F f) {
    if (what >= kHandleBase) {
        Voice* v = voice_of_handle(what);
        if (v) f(v);
        return;
    }
    for (Voice*& v : g_voices) {
        if (!v) continue;
        if ((what >= kStreamBase && v->stream == what) ||
            (what >= 0 && what < kStreamBase && v->asset == what))
            f(v);
    }
}

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}

GMLFN(audio_play_sound) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    int what = (int)(double)args[0];
    bool loop = argc > 2 && gml_truthy(args[2]);
    Voice* v = start_voice(what, loop);
    if (!v) return Value(-1.0);
    return Value((double)(kHandleBase + store_voice(v)));
}

GMLFN(audio_stop_sound) {
    (void)self;
    if (argc < 1) return Value();
    int what = (int)(double)args[0];
    for (size_t i = 0; i < g_voices.size(); ++i) {
        Voice* v = g_voices[i];
        if (!v) continue;
        bool match = (what >= kHandleBase && (int)i == what - kHandleBase) ||
                     (what >= kStreamBase && what < kHandleBase && v->stream == what) ||
                     (what >= 0 && what < kStreamBase && v->asset == what);
        if (match) {
            free_voice(v);
            g_voices[i] = nullptr;
        }
    }
    return Value();
}

GMLFN(audio_stop_all) {
    (void)self; (void)args; (void)argc;
    for (size_t i = 0; i < g_voices.size(); ++i) {
        if (g_voices[i]) {
            free_voice(g_voices[i]);
            g_voices[i] = nullptr;
        }
    }
    return Value();
}

GMLFN(audio_is_playing) {
    (void)self;
    if (argc < 1) return Value(0.0);
    int what = (int)(double)args[0];
    bool playing = false;
    for_matching(what, [&](Voice* v) {
        if (v->active && (v->paused || ma_sound_is_playing(&v->snd))) playing = true;
    });
    return Value(playing);
}

GMLFN(audio_pause_sound) {
    (void)self;
    if (argc < 1) return Value();
    for_matching((int)(double)args[0], [](Voice* v) {
        if (v->active && !v->paused) {
            ma_sound_stop(&v->snd);
            v->paused = true;
        }
    });
    return Value();
}

GMLFN(audio_resume_sound) {
    (void)self;
    if (argc < 1) return Value();
    for_matching((int)(double)args[0], [](Voice* v) {
        if (v->active && v->paused) {
            ma_sound_start(&v->snd);
            v->paused = false;
        }
    });
    return Value();
}

GMLFN(audio_pause_all) {
    (void)self; (void)args; (void)argc;
    for (Voice* v : g_voices)
        if (v && v->active && !v->paused) {
            ma_sound_stop(&v->snd);
            v->paused = true;
        }
    return Value();
}

GMLFN(audio_resume_all) {
    (void)self; (void)args; (void)argc;
    for (Voice* v : g_voices)
        if (v && v->active && v->paused) {
            ma_sound_start(&v->snd);
            v->paused = false;
        }
    return Value();
}

GMLFN(audio_sound_gain) {
    (void)self;
    if (argc < 2) return Value();
    float target = (float)(double)args[1];
    double time_ms = A(args, argc, 2);
    for_matching((int)(double)args[0], [&](Voice* v) {
        if (time_ms <= 0) {
            v->gain = target;
            v->fade_target = -1;
            ma_sound_set_volume(&v->snd, v->base_gain * v->gain);
        } else {
            v->fade_target = target;
            double steps = time_ms / 1000.0 * 60.0;
            v->fade_step = (float)((target - v->gain) / (steps > 1 ? steps : 1));
        }
    });
    return Value();
}

GMLFN(audio_sound_pitch) {
    (void)self;
    if (argc < 2) return Value();
    float p = (float)(double)args[1];
    for_matching((int)(double)args[0], [&](Voice* v) { ma_sound_set_pitch(&v->snd, p); });
    return Value();
}

GMLFN(audio_sound_get_track_position) {
    (void)self;
    if (argc < 1) return Value(0.0);
    double pos = 0;
    for_matching((int)(double)args[0], [&](Voice* v) {
        float sec = 0;
        if (ma_sound_get_cursor_in_seconds(&v->snd, &sec) == MA_SUCCESS) pos = sec;
    });
    return Value(pos);
}

GMLFN(audio_sound_set_track_position) {
    (void)self;
    if (argc < 2) return Value();
    double sec = (double)args[1];
    for_matching((int)(double)args[0], [&](Voice* v) {
        ma_uint32 rate = ma_engine_get_sample_rate(&g_engine);
        if (v->buffer) rate = v->buffer->ref.sampleRate;
        ma_sound_seek_to_pcm_frame(&v->snd, (ma_uint64)(sec * rate));
    });
    return Value();
}

GMLFN(audio_set_master_gain) {
    (void)self;
    if (ensure_engine() && argc >= 1)
        ma_engine_set_volume(&g_engine, (float)(double)args[argc - 1]);
    return Value();
}

GMLFN(audio_master_gain) {
    (void)self;
    if (ensure_engine() && argc >= 1)
        ma_engine_set_volume(&g_engine, (float)(double)args[argc - 1]);
    return Value();
}

GMLFN(audio_get_master_gain) {
    (void)self; (void)args; (void)argc;
    if (!ensure_engine()) return Value(1.0);
    return Value((double)ma_engine_get_volume(&g_engine));
}

GMLFN(audio_create_stream) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    return Value((double)kwik_stream_register((std::string)args[0]));
}

GMLFN(audio_destroy_stream) { return audio_stop_sound(self, args, argc); }

GMLFN(audio_group_is_loaded) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(audio_group_load) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(audio_group_set_gain) { (void)self; (void)args; (void)argc; return Value(); }

double kwik_voice_gain(int what) {
    double g = 1.0;
    for_matching(what, [&](Voice* v) { g = v->gain; });
    return g;
}

double kwik_voice_pitch(int what) {
    double p = 1.0;
    for_matching(what, [&](Voice* v) { p = ma_sound_get_pitch(&v->snd); });
    return p;
}

bool kwik_voice_paused(int what) {
    bool paused = false;
    for_matching(what, [&](Voice* v) { paused = v->paused; });
    return paused;
}

double kwik_sound_length_seconds(int what) {
    double len = 0.0;
    bool got = false;
    for_matching(what, [&](Voice* v) {
        float sec = 0;
        if (ma_sound_get_length_in_seconds(&v->snd, &sec) == MA_SUCCESS) {
            len = sec;
            got = true;
        }
    });
    if (got) return len;
    if (what >= 0 && what < g_sound_count && ensure_engine()) {
        Voice* v = start_voice(what, false);
        if (v) {
            ma_sound_stop(&v->snd);
            float sec = 0;
            if (ma_sound_get_length_in_seconds(&v->snd, &sec) == MA_SUCCESS) len = sec;
            free_voice(v);
        }
    }
    return len;
}

void kwik_audio_update() {
    for (size_t i = 0; i < g_voices.size(); ++i) {
        Voice* v = g_voices[i];
        if (!v) continue;
        if (v->active && !v->paused && !ma_sound_is_playing(&v->snd) &&
            !ma_sound_is_looping(&v->snd)) {
            free_voice(v);
            g_voices[i] = nullptr;
            continue;
        }
        if (v->fade_target >= 0) {
            v->gain += v->fade_step;
            bool done = (v->fade_step >= 0 && v->gain >= v->fade_target) ||
                        (v->fade_step < 0 && v->gain <= v->fade_target);
            if (done) {
                v->gain = v->fade_target;
                v->fade_target = -1;
            }
            ma_sound_set_volume(&v->snd, v->base_gain * v->gain);
        }
    }
}

void kwik_audio_shutdown() {
    for (size_t i = 0; i < g_voices.size(); ++i) {
        if (g_voices[i]) {
            free_voice(g_voices[i]);
            g_voices[i] = nullptr;
        }
    }
    if (g_engine_ok) {
        ma_engine_uninit(&g_engine);
        g_engine_ok = false;
        g_engine_tried = false;
    }
}

}
