// User-authored materials: a custom fragment shader plus a uniform block,
// attachable to any element. Not installed and not part of the public API.
//
// A material replaces the fragment stage only. The vertex stage stays opane's,
// so a custom material lays out, batches, and positions exactly as a built-in
// primitive does, and a user cannot break the interface by writing one.

#pragma once

#include "Internal.h"

#include <string>
#include <vector>

namespace opane
{

class Renderer;

namespace detail
{

// Mirrors the OpaneMaterial cbuffer in material.hlsli. The layouts must agree.
struct MaterialUniformBlock
{
    float Params[8][4] = {};
    float Time[4] = {};
};

struct MaterialRecord
{
    uint32_t Generation = 0;
    bool Alive = false;

    SDL_GPUGraphicsPipeline* Pipeline = nullptr;
    MaterialUniformBlock Uniforms;
    std::vector<std::string> UniformNames;

    // Set only for materials compiled from a file, which are the ones that can
    // hot reload.
    std::string SourcePath;
    std::string EntryPoint;
    bool HotReload = false;
    int64_t SourceTimestamp = 0;
};

class MaterialStore
{
public:
    void Initialize(Renderer* renderer);
    void Shutdown();

    MaterialId Create(const MaterialDesc& desc);
    void Destroy(MaterialId id);

    MaterialRecord* Resolve(const MaterialId& id);
    const MaterialRecord* Resolve(const MaterialId& id) const;

    void SetUniform(const MaterialId& id, const std::string& name, float x, float y, float z, float w);

    // Moves a uniform to a new value over time instead of at once. Animating
    // a uniform that is already moving replaces that animation, starting from
    // wherever the value had reached, so a value chased back and forth never
    // jumps.
    void Animate(const MaterialId& id, const std::string& name, const float target[4], float seconds,
                 Easing easing);
    void CancelAnimations(const MaterialId& id);

    // Advances the clock every material sees through OpaneTime, and every
    // animation in flight.
    void Tick(float totalSeconds, float deltaSeconds);

    // Recompiles any file-backed material whose source changed. A failed
    // compile keeps the working pipeline and reports the compiler's message.
    void ReloadChanged();

private:
    bool CompileToBytecode(const std::string& sourcePath, const std::string& entryPoint,
                           std::vector<unsigned char>& outBytecode, std::string& outError) const;
    // From bytecode in any of the formats, or from what CompileToBytecode
    // produced, which is in the device's own.
    SDL_GPUGraphicsPipeline* BuildPipeline(const ShaderBytecode& bytecode, const std::string& entryPoint) const;
    SDL_GPUGraphicsPipeline* BuildPipeline(const std::vector<unsigned char>& compiled,
                                           const std::string& entryPoint) const;

    // A uniform on its way from one value to another.
    struct UniformAnimation
    {
        MaterialId Material;
        size_t Slot = 0;
        float From[4] = {};
        float To[4] = {};
        float Elapsed = 0.0f;
        float Duration = 0.0f;
        Easing Curve = Easing::Smooth;
    };

    Renderer* m_Renderer = nullptr;
    std::vector<MaterialRecord> m_Materials;
    std::vector<uint32_t> m_FreeMaterials;
    std::vector<UniformAnimation> m_Animations;

    float m_TotalSeconds = 0.0f;
    float m_DeltaSeconds = 0.0f;
    float m_ReloadTimer = 0.0f;
};

} // namespace detail
} // namespace opane
