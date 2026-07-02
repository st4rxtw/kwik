#include "gml_runtime.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels,
                                        int* sample_rate, short** output);

namespace gml {

static ma_engine g_engine;
static bool g_engine_ok = false;
static bool g_engine_tried = false;
static std::set<int> g_registered;

struct Voice {
    ma_sound snd;
    ma_audio_buffer* buffer = nullptr;
    short* pcm = nullptr;
    bool active = false;
};
static std::vector<Voice*> g_voices;

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

static bool init_voice_ogg(Voice* v, const unsigned char* data, unsigned int size) {
    int channels = 0, sample_rate = 0;
    short* pcm = nullptr;
    int frames = stb_vorbis_decode_memory(data, (int)size, &channels, &sample_rate, &pcm);
    if (frames <= 0 || !pcm) return false;

    v->pcm = pcm;
    v->buffer = new ma_audio_buffer();
    ma_audio_buffer_config cfg =
        ma_audio_buffer_config_init(ma_format_s16, (ma_uint32)channels, (ma_uint64)frames, pcm, nullptr);
    cfg.sampleRate = (ma_uint32)sample_rate;
    if (ma_audio_buffer_init(&cfg, v->buffer) != MA_SUCCESS) {
        delete v->buffer;
        v->buffer = nullptr;
        return false;
    }
    return ma_sound_init_from_data_source(&g_engine, v->buffer, 0, nullptr, &v->snd) == MA_SUCCESS;
}

static bool ensure_engine() {
    if (g_engine_tried) return g_engine_ok;
    g_engine_tried = true;
    g_engine_ok = ma_engine_init(nullptr, &g_engine) == MA_SUCCESS;
    if (!g_engine_ok) std::fprintf(stderr, "[audio] engine init failed\n");
    return g_engine_ok;
}

Value audio_play_sound(const Value& snd, const Value&, const Value& loop) {
    if (!ensure_engine()) return Value(-1.0);

    int asset = (int)(double)snd;
    int audio_id = asset;
    if (g_sound_audio_id && asset >= 0 && asset < g_sound_map_count)
        audio_id = g_sound_audio_id[asset];

    unsigned int size = 0;
    int type = 0;
    const unsigned char* data = kwik_sound_blob(audio_id, size, type);
    if (!data || size == 0) return Value(-1.0);

    Voice* v = new Voice();
    bool ok;
    if (type == 2) {
        ok = init_voice_ogg(v, data, size);
    } else {
        char name[32];
        std::snprintf(name, sizeof(name), "snd%d", audio_id);
        if (!g_registered.count(audio_id)) {
            ma_resource_manager* rm = ma_engine_get_resource_manager(&g_engine);
            if (ma_resource_manager_register_encoded_data(rm, name, data, size) == MA_SUCCESS)
                g_registered.insert(audio_id);
        }
        ok = g_registered.count(audio_id) &&
             ma_sound_init_from_file(&g_engine, name, MA_SOUND_FLAG_DECODE, nullptr, nullptr,
                                     &v->snd) == MA_SUCCESS;
    }
    if (!ok) {
        if (v->buffer) { ma_audio_buffer_uninit(v->buffer); delete v->buffer; }
        if (v->pcm) std::free(v->pcm);
        delete v;
        return Value(-1.0);
    }
    ma_sound_set_looping(&v->snd, gml_truthy(loop) ? MA_TRUE : MA_FALSE);
    ma_sound_start(&v->snd);
    v->active = true;

    int handle = -1;
    for (size_t i = 0; i < g_voices.size(); ++i) {
        if (!g_voices[i]) {
            g_voices[i] = v;
            handle = (int)i;
            break;
        }
    }
    if (handle < 0) {
        handle = (int)g_voices.size();
        g_voices.push_back(v);
    }
    return Value((double)(handle + 1));
}

Value audio_stop_sound(const Value& v) {
    int idx = (int)(double)v - 1;
    if (idx >= 0 && idx < (int)g_voices.size() && g_voices[idx]) {
        free_voice(g_voices[idx]);
        g_voices[idx] = nullptr;
    }
    return Value();
}

}
