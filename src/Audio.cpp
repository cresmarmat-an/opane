// miniaudio pulls in windows.h, whose min and max macros would otherwise break
// every std::max and std::clamp below.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Audio.h"

#include "Assets.h"

#include <algorithm>

// miniaudio is a single-header library. Encoding, the null backend, and the
// generation nodes are not used, so they are compiled out to keep build time
// and binary size down.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505) // unreferenced local function removed
#pragma warning(disable : 4996) // deprecated CRT function
#pragma warning(disable : 4244) // conversion, possible loss of data
#pragma warning(disable : 4245) // signed/unsigned mismatch
#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4018) // signed/unsigned comparison
#pragma warning(disable : 4456) // declaration hides previous
#pragma warning(disable : 4457) // declaration hides function parameter
#pragma warning(disable : 4701) // potentially uninitialized local variable
#pragma warning(disable : 4127) // conditional expression is constant
#pragma warning(disable : 4702) // unreachable code
#pragma warning(disable : 4204) // non-constant aggregate initializer
#pragma warning(disable : 4310) // cast truncates constant value
#pragma warning(disable : 4146) // unary minus applied to unsigned
#pragma warning(disable : 4389) // signed/unsigned mismatch
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

// Ogg Vorbis comes from stb_vorbis, declared before miniaudio so miniaudio
// enables its Vorbis decoder, and implemented after it.
#include "VorbisNames.h"
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace opane::detail
{
namespace
{

size_t GroupIndex(AudioGroup group)
{
    return static_cast<size_t>(group);
}

const char* GroupName(AudioGroup group)
{
    switch (group)
    {
        case AudioGroup::Master:    return "master";
        case AudioGroup::Music:     return "music";
        case AudioGroup::Interface: return "interface";
        case AudioGroup::Effects:   return "effects";
    }
    return "?";
}

} // namespace

struct AudioEngine::ExternalSource
{
    // First, so the struct is usable where miniaudio expects a data source.
    ma_data_source_base Base;

    void (*Read)(void* user, float* frames, uint32_t frameCount) = nullptr;
    void* User = nullptr;
    ma_uint32 Channels = 2;
    ma_uint32 SampleRate = 48000;
    ma_sound Sound;
    int Id = -1;
};

namespace
{

ma_result ExternalRead(ma_data_source* dataSource, void* framesOut, ma_uint64 frameCount, ma_uint64* framesRead)
{
    auto* source = reinterpret_cast<AudioEngine::ExternalSource*>(dataSource);
    source->Read(source->User, static_cast<float*>(framesOut), static_cast<uint32_t>(frameCount));
    if (framesRead != nullptr)
    {
        *framesRead = frameCount;
    }
    return MA_SUCCESS;
}

ma_result ExternalSeek(ma_data_source*, ma_uint64)
{
    return MA_NOT_IMPLEMENTED;
}

ma_result ExternalFormat(ma_data_source* dataSource, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate,
                         ma_channel* channelMap, size_t channelMapCapacity)
{
    auto* source = reinterpret_cast<AudioEngine::ExternalSource*>(dataSource);
    *format = ma_format_f32;
    *channels = source->Channels;
    *sampleRate = source->SampleRate;
    ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, source->Channels);
    return MA_SUCCESS;
}

ma_result ExternalCursor(ma_data_source*, ma_uint64* cursor)
{
    *cursor = 0;
    return MA_SUCCESS;
}

ma_result ExternalLength(ma_data_source*, ma_uint64* length)
{
    *length = 0;
    return MA_NOT_IMPLEMENTED;
}

const ma_data_source_vtable g_ExternalVtable = { ExternalRead, ExternalSeek, ExternalFormat,
                                                 ExternalCursor, ExternalLength, nullptr, 0 };

} // namespace

bool AudioEngine::Initialize()
{
    m_Engine = new ma_engine();

    ma_engine_config config = ma_engine_config_init();

    if (ma_engine_init(&config, m_Engine) != MA_SUCCESS)
    {
        delete m_Engine;
        m_Engine = nullptr;

        // No audio device is a normal situation on a headless machine. The
        // program keeps running and every audio call becomes a no-op.
        LogMessage(LogLevel::Warning, "audio",
                   "No audio device could be opened. Sound is disabled; everything else runs.");
        return false;
    }

    // Master first, then the rest parented to it, so the master volume scales
    // every other group.
    for (size_t index = 0; index < 4; ++index)
    {
        ma_sound* parent = (index == GroupIndex(AudioGroup::Master)) ? nullptr : m_Groups[GroupIndex(AudioGroup::Master)];
        m_Groups[index] = new ma_sound();
        if (ma_sound_group_init(m_Engine, 0, parent, m_Groups[index]) != MA_SUCCESS)
        {
            delete m_Groups[index];
            m_Groups[index] = nullptr;
            LogMessage(LogLevel::Warning, "audio", "Could not create the %s volume group.",
                       GroupName(static_cast<AudioGroup>(index)));
            continue;
        }
        ma_sound_group_set_volume(m_Groups[index], m_GroupVolumes[index]);
    }

    LogMessage(LogLevel::Info, "audio", "Audio running at %u Hz.",
               ma_engine_get_sample_rate(m_Engine));
    return true;
}

void AudioEngine::Shutdown()
{
    if (m_Engine == nullptr)
    {
        return;
    }

    for (ExternalSource* source : m_External)
    {
        ma_sound_uninit(&source->Sound);
        ma_data_source_uninit(&source->Base);
        delete source;
    }
    m_External.clear();

    for (SoundRecord& record : m_Sounds)
    {
        if (record.Alive && record.Sound != nullptr)
        {
            ma_sound_uninit(record.Sound);
            delete record.Sound;
        }
    }
    m_Sounds.clear();
    m_FreeSounds.clear();

    for (size_t index = 0; index < 4; ++index)
    {
        if (m_Groups[index] != nullptr)
        {
            ma_sound_group_uninit(m_Groups[index]);
            delete m_Groups[index];
            m_Groups[index] = nullptr;
        }
    }

    ma_engine_uninit(m_Engine);
    delete m_Engine;
    m_Engine = nullptr;
}

int AudioEngine::AddExternalSource(void (*read)(void* user, float* frames, uint32_t frameCount), void* user)
{
    if (m_Engine == nullptr || read == nullptr)
    {
        return -1;
    }

    auto* source = new ExternalSource();
    source->Read = read;
    source->User = user;
    source->Channels = ma_engine_get_channels(m_Engine);
    source->SampleRate = ma_engine_get_sample_rate(m_Engine);

    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &g_ExternalVtable;
    if (ma_data_source_init(&config, &source->Base) != MA_SUCCESS)
    {
        delete source;
        return -1;
    }

    // Already mixed and placed by the guest, so it is neither spatialized nor
    // pitched again; it plays through the master group like everything else.
    const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    if (ma_sound_init_from_data_source(m_Engine, &source->Base, flags, GroupHandle(AudioGroup::Master), &source->Sound) !=
        MA_SUCCESS)
    {
        ma_data_source_uninit(&source->Base);
        delete source;
        return -1;
    }

    static int nextId = 0;
    source->Id = nextId++;
    m_External.push_back(source);
    ma_sound_start(&source->Sound);

    LogMessage(LogLevel::Info, "audio", "Mixing another library's audio into this output.");
    return source->Id;
}

void AudioEngine::RemoveExternalSource(int id)
{
    for (auto entry = m_External.begin(); entry != m_External.end(); ++entry)
    {
        if ((*entry)->Id == id)
        {
            // Uninitializing detaches the sound from the graph under the
            // engine's own lock, so the audio thread never reads it afterwards.
            ma_sound_uninit(&(*entry)->Sound);
            ma_data_source_uninit(&(*entry)->Base);
            delete *entry;
            m_External.erase(entry);
            return;
        }
    }
}

uint32_t AudioEngine::GetSampleRate() const
{
    return m_Engine != nullptr ? ma_engine_get_sample_rate(m_Engine) : 0;
}

uint32_t AudioEngine::GetChannels() const
{
    return m_Engine != nullptr ? ma_engine_get_channels(m_Engine) : 0;
}

ma_sound* AudioEngine::GroupHandle(AudioGroup group) const
{
    const size_t index = GroupIndex(group);
    return index < 4 ? m_Groups[index] : nullptr;
}

SoundId AudioEngine::Load(const std::string& path, AudioGroup group, bool streaming)
{
    SoundId id;

    if (m_Engine == nullptr)
    {
        return id;
    }

    // A missing sound is a warning, not a failure. The program keeps running
    // in silence rather than stopping.
    const std::string resolved = ResolveAsset(path, "audio", LogLevel::Warning);
    if (resolved.empty())
    {
        return id;
    }

    auto* sound = new ma_sound();

    // Music streams from disk; short effects decode once so they can be
    // retriggered without touching the file system again.
    ma_uint32 flags = streaming ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;

    const ma_result result =
        ma_sound_init_from_file(m_Engine, resolved.c_str(), flags, GroupHandle(group), nullptr, sound);

    if (result != MA_SUCCESS)
    {
        delete sound;
        LogMessage(LogLevel::Warning, "audio", "Could not decode \"%s\"; it will be silent.",
                   resolved.c_str());
        return id;
    }

    uint32_t index;
    if (!m_FreeSounds.empty())
    {
        index = m_FreeSounds.back();
        m_FreeSounds.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_Sounds.size());
        m_Sounds.emplace_back();
    }

    SoundRecord& record = m_Sounds[index];
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }

    record.Alive = true;
    record.Sound = sound;
    record.Group = group;
    record.Path = path;
    record.Streaming = streaming;

    id.Index = index;
    id.Generation = record.Generation;
    return id;
}

void AudioEngine::Destroy(SoundId id)
{
    SoundRecord* record = Resolve(id);
    if (record == nullptr)
    {
        return;
    }

    ma_sound_uninit(record->Sound);
    delete record->Sound;

    record->Sound = nullptr;
    record->Alive = false;
    record->Path.clear();

    ++record->Generation;
    if (record->Generation == 0)
    {
        record->Generation = 1;
    }

    m_FreeSounds.push_back(id.Index);
}

SoundRecord* AudioEngine::Resolve(const SoundId& id)
{
    if (!id.IsValid() || id.Index >= m_Sounds.size())
    {
        return nullptr;
    }

    SoundRecord& record = m_Sounds[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        return nullptr;
    }
    return &record;
}

const SoundRecord* AudioEngine::Resolve(const SoundId& id) const
{
    return const_cast<AudioEngine*>(this)->Resolve(id);
}

void AudioEngine::Play(const SoundId& id, const SoundPlayback& playback)
{
    SoundRecord* record = Resolve(id);
    if (record == nullptr)
    {
        return;
    }

    ma_sound_set_volume(record->Sound, playback.Volume);
    ma_sound_set_pitch(record->Sound, std::max(0.01f, playback.Pitch));
    ma_sound_set_pan(record->Sound, std::clamp(playback.Pan, -1.0f, 1.0f));
    ma_sound_set_looping(record->Sound, playback.Looping ? MA_TRUE : MA_FALSE);

    if (playback.Restart)
    {
        ma_sound_seek_to_pcm_frame(record->Sound, 0);
    }

    ma_sound_start(record->Sound);
}

void AudioEngine::PlayAt(const SoundId& id, Vec3 position, const SpatialPlayback& playback)
{
    SoundRecord* record = Resolve(id);
    if (record == nullptr)
    {
        return;
    }

    // Spatialization is off by default so that a sound used flatly costs
    // nothing; turning it on here makes it positional from this play onward.
    ma_sound_set_spatialization_enabled(record->Sound, MA_TRUE);
    ma_sound_set_attenuation_model(record->Sound, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(record->Sound, std::max(0.01f, playback.MinDistance));
    ma_sound_set_max_distance(record->Sound, std::max(playback.MinDistance, playback.MaxDistance));
    ma_sound_set_rolloff(record->Sound, std::max(0.0f, playback.Rolloff));
    ma_sound_set_position(record->Sound, position.X, position.Y, position.Z);

    ma_sound_set_volume(record->Sound, playback.Volume);
    ma_sound_set_pitch(record->Sound, std::max(0.01f, playback.Pitch));
    ma_sound_set_looping(record->Sound, playback.Looping ? MA_TRUE : MA_FALSE);

    if (playback.Restart)
    {
        ma_sound_seek_to_pcm_frame(record->Sound, 0);
    }

    ma_sound_start(record->Sound);
}

void AudioEngine::SetPosition(const SoundId& id, Vec3 position)
{
    if (SoundRecord* record = Resolve(id))
    {
        ma_sound_set_position(record->Sound, position.X, position.Y, position.Z);
    }
}

void AudioEngine::SetListener(Vec3 position, Vec3 forward, Vec3 up)
{
    if (m_Engine == nullptr)
    {
        return;
    }

    ma_engine_listener_set_position(m_Engine, 0, position.X, position.Y, position.Z);
    ma_engine_listener_set_direction(m_Engine, 0, forward.X, forward.Y, forward.Z);
    ma_engine_listener_set_world_up(m_Engine, 0, up.X, up.Y, up.Z);
}

void AudioEngine::Stop(const SoundId& id)
{
    if (SoundRecord* record = Resolve(id))
    {
        ma_sound_stop(record->Sound);
    }
}

void AudioEngine::StopGroup(AudioGroup group)
{
    for (SoundRecord& record : m_Sounds)
    {
        if (record.Alive && record.Group == group && record.Sound != nullptr)
        {
            ma_sound_stop(record.Sound);
        }
    }
}

bool AudioEngine::IsPlaying(const SoundId& id) const
{
    const SoundRecord* record = Resolve(id);
    return record != nullptr && ma_sound_is_playing(record->Sound) == MA_TRUE;
}

void AudioEngine::SetGroupVolume(AudioGroup group, float volume)
{
    const size_t index = GroupIndex(group);
    if (index >= 4)
    {
        return;
    }

    m_GroupVolumes[index] = std::max(0.0f, volume);

    if (m_Groups[index] != nullptr)
    {
        ma_sound_group_set_volume(m_Groups[index], m_GroupVolumes[index]);
    }

    // Master scales the engine itself, so it covers every group at once.
    if (group == AudioGroup::Master && m_Engine != nullptr)
    {
        ma_engine_set_volume(m_Engine, m_GroupVolumes[index]);
    }
}

float AudioEngine::GetGroupVolume(AudioGroup group) const
{
    const size_t index = GroupIndex(group);
    return index < 4 ? m_GroupVolumes[index] : 0.0f;
}

void AudioEngine::SetSoundVolume(const SoundId& id, float volume)
{
    if (SoundRecord* record = Resolve(id))
    {
        ma_sound_set_volume(record->Sound, std::max(0.0f, volume));
    }
}

} // namespace opane::detail
