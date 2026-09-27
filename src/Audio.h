// Audio: music, sound effects, and volume groups.
// Not installed and not part of the public API.
//
// There is one audio engine per process. SDL's audio subsystem is never
// initialized, so there is no second device.

#pragma once

#include "Internal.h"

#include <string>
#include <vector>

struct ma_engine;
struct ma_sound;

// miniaudio defines ma_sound_group as an alias of ma_sound rather than a
// distinct type, so groups are held as ma_sound* here.

namespace opane::detail
{

struct SoundRecord
{
    uint32_t Generation = 0;
    bool Alive = false;

    ma_sound* Sound = nullptr;
    AudioGroup Group = AudioGroup::Effects;
    std::string Path;
    bool Streaming = false;
};

class AudioEngine
{
public:
    bool Initialize();
    void Shutdown();

    bool IsRunning() const { return m_Engine != nullptr; }

    SoundId Load(const std::string& path, AudioGroup group, bool streaming);
    void Destroy(SoundId id);

    void Play(const SoundId& id, const SoundPlayback& playback);
    void PlayAt(const SoundId& id, Vec3 position, const SpatialPlayback& playback);
    void SetPosition(const SoundId& id, Vec3 position);
    void SetListener(Vec3 position, Vec3 forward, Vec3 up);
    void Stop(const SoundId& id);
    void StopGroup(AudioGroup group);
    bool IsPlaying(const SoundId& id) const;

    void SetGroupVolume(AudioGroup group, float volume);
    float GetGroupVolume(AudioGroup group) const;

    void SetSoundVolume(const SoundId& id, float volume);

    // Another library's mix, pulled on the audio thread and played through the
    // master group. This is how a guest shares this engine's device rather
    // than opening a second one.
    int AddExternalSource(void (*read)(void* user, float* frames, uint32_t frameCount), void* user);
    void RemoveExternalSource(int id);

    uint32_t GetSampleRate() const;
    uint32_t GetChannels() const;

    struct ExternalSource;

private:

    SoundRecord* Resolve(const SoundId& id);
    const SoundRecord* Resolve(const SoundId& id) const;
    ma_sound* GroupHandle(AudioGroup group) const;

    ma_engine* m_Engine = nullptr;

    // One group per AudioGroup value, all parented to the engine so the master
    // volume scales everything.
    ma_sound* m_Groups[4] = {};
    float m_GroupVolumes[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    std::vector<SoundRecord> m_Sounds;
    std::vector<uint32_t> m_FreeSounds;

    // Heap-allocated so each keeps its address while the audio thread reads it.
    std::vector<ExternalSource*> m_External;
};

} // namespace opane::detail
