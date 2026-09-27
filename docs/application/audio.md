# Audio

opane has one audio engine for the whole process, shared by music, sound
effects, interface sounds, and any world a library such as ludifex plays into
it.

## Loading and playing

```cpp
opane::SoundId click = app.LoadSound("click.wav");   // decoded once, kept in memory
opane::SoundId music = app.LoadMusic("theme.ogg");   // streamed from disk

app.PlaySound(click);
app.PlaySound(click, { .Volume = 0.6f, .Pitch = 1.2f, .Pan = -0.3f });
app.PlaySound(music, { .Volume = 0.5f, .Looping = true });

app.IsSoundPlaying(music);
app.StopSound(music);
app.DestroySound(click);
```

WAV, FLAC, MP3, and Ogg Vorbis files are read through the
[asset roots](../getting-started/finding-files.md). Use `LoadSound` for short
effects: they are decoded once and can be played again without touching the
disk. Use `LoadMusic` for long tracks: they are streamed.

`SoundPlayback` fields:

| Field | Default | Meaning |
|---|---|---|
| `Volume` | 1 | Loudness, multiplied by the group volumes. |
| `Pitch` | 1 | Playback speed; 2 is an octave up. |
| `Pan` | 0 | -1 is fully left, 1 fully right. |
| `Looping` | false | Start again at the end. |
| `Restart` | true | Playing a sound that is already playing starts it again from the beginning. With `false`, the call only updates its settings and lets it continue. |

## Volume groups

Each sound belongs to a group, so a player can turn music down without
affecting interface sounds:

```cpp
opane::SoundId hover = app.LoadSound("hover.wav", opane::AudioGroup::Interface);

app.SetGroupVolume(opane::AudioGroup::Master, 0.8f);
app.SetGroupVolume(opane::AudioGroup::Music, 0.4f);
app.GetGroupVolume(opane::AudioGroup::Effects);
app.StopGroup(opane::AudioGroup::Effects);
```

The groups are `Master`, `Music`, `Interface`, and `Effects`. `LoadSound` puts
a sound in `Effects` unless you name another group, and `LoadMusic` puts it in
`Music`. Every group plays through `Master`.

## Positional sound

A sound can be placed in space and heard from a listener:

```cpp
app.PlaySoundAt(engineHum, { 4.0f, 0.0f, -2.0f }, {
    .Volume = 1.0f,
    .Looping = true,
    .MinDistance = 1.0f,    // full volume within this distance
    .MaxDistance = 40.0f,   // silent beyond this distance
    .Rolloff = 1.0f,        // how quickly it fades between the two
});

app.SetSoundPosition(engineHum, { 5.0f, 0.0f, -2.0f });   // move it while it plays

// Where the listener is and which way it faces. Update it from your camera.
app.SetListener(cameraPosition, cameraForward, { 0.0f, 1.0f, 0.0f });
```

Positions are in whatever units you use for your scene. Until you call
`SetListener`, the listener is at the origin facing -Z.

## A world's sound

When a ludifex world runs in the same program, its sound is mixed into
opane's output. The world finds opane's engine through the same connection
that shares the GPU device (see [Hosting a world](hosting-a-world.md)), and its
already-mixed, already-positioned audio is added to opane's mix through the
`Master` group. So `SetGroupVolume(AudioGroup::Master, ...)` turns down the
interface and the world together. Nothing needs to be set up. Without opane,
the world opens an audio device of its own.

## No audio device

A machine with no audio device is not an error. `IsAudioRunning()` returns
false, a warning is logged once, and every audio call does nothing, so the rest
of the program runs unchanged. A file that cannot be loaded gives an invalid
`SoundId` and a warning; playing it does nothing.

## Limitations

- Each `SoundId` is one voice. Playing it while it is already playing restarts
  it instead of starting a second copy. To play the same effect several times
  at once, load it several times.
- There are no audio effects such as reverb, echo, or filters, and no
  recording or microphone input.
- Positional sound has distance attenuation only. For Doppler shift,
  directional cones, voice limits, and occlusion, use a ludifex world's
  [sound](https://cresmarmat-an.github.io/ludifex/sound/sound/).
- Sound plays on the system's default output device. There is no way to list
  devices or pick another one.
