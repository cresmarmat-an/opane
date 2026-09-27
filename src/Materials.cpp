#include "Materials.h"

#include "Assets.h"
#include "Backend.h"
#include "Renderer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace opane
{
namespace detail
{
namespace
{

// Checking the file system every frame would be wasteful; a few times a second
// is indistinguishable while editing a shader.
constexpr float ReloadInterval = 0.25f;

int64_t GetTimestamp(const std::string& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return 0;
    }
    return static_cast<int64_t>(time.time_since_epoch().count());
}

} // namespace

void MaterialStore::Initialize(Renderer* renderer)
{
    m_Renderer = renderer;
}

void MaterialStore::Shutdown()
{
    for (MaterialRecord& record : m_Materials)
    {
        if (record.Alive && record.Pipeline != nullptr && m_Renderer != nullptr)
        {
            m_Renderer->ReleasePipeline(record.Pipeline);
        }
    }
    m_Materials.clear();
    m_FreeMaterials.clear();
    m_Renderer = nullptr;
}

bool MaterialStore::CompileToBytecode(const std::string& sourcePath, const std::string& entryPoint,
                                      std::vector<unsigned char>& outBytecode,
                                      std::string& outError) const
{
    return CompileMaterialSource(sourcePath, entryPoint, DeviceShaderFormat(m_Renderer->GetDevice()), outBytecode,
                                 outError);
}

SDL_GPUGraphicsPipeline* MaterialStore::BuildPipeline(const std::vector<unsigned char>& compiled,
                                                      const std::string& entryPoint) const
{
    ShaderBytecode bytecode;
    switch (DeviceShaderFormat(m_Renderer->GetDevice()))
    {
    case SDL_GPU_SHADERFORMAT_DXIL:
        bytecode.Dxil = compiled.data();
        bytecode.DxilSize = compiled.size();
        break;
    case SDL_GPU_SHADERFORMAT_SPIRV:
        bytecode.Spirv = compiled.data();
        bytecode.SpirvSize = compiled.size();
        break;
    default:
        bytecode.Msl = compiled.data();
        bytecode.MslSize = compiled.size();
        break;
    }
    return BuildPipeline(bytecode, entryPoint);
}

SDL_GPUGraphicsPipeline* MaterialStore::BuildPipeline(const ShaderBytecode& bytecode,
                                                      const std::string& entryPoint) const
{
    if (m_Renderer == nullptr || bytecode.IsEmpty())
    {
        return nullptr;
    }
    return m_Renderer->CreatePipelineForFragmentShader(bytecode, entryPoint.c_str());
}

MaterialId MaterialStore::Create(const MaterialDesc& desc)
{
    MaterialId id;

    if (m_Renderer == nullptr)
    {
        return id;
    }

    if (desc.Uniforms.size() > 8)
    {
        LogMessage(LogLevel::Error, "material",
                   "A material may declare at most 8 uniforms; %zu were given.",
                   desc.Uniforms.size());
        return id;
    }

    std::vector<unsigned char> compiled;
    const bool fromFile = desc.Bytecode.IsEmpty() && !desc.ShaderPath.empty();

    // The shader is found through the asset roots like any other file, and the
    // resolved path is what hot reload then watches.
    std::string shaderPath;
    if (fromFile)
    {
        shaderPath = ResolveAsset(desc.ShaderPath, "material");
        if (shaderPath.empty())
        {
            return id;
        }

        std::string error;
        if (!CompileToBytecode(shaderPath, desc.EntryPoint, compiled, error))
        {
            LogMessage(LogLevel::Error, "material", "Could not compile \"%s\":\n%s",
                       shaderPath.c_str(), error.c_str());
            return id;
        }
    }
    else if (desc.Bytecode.IsEmpty())
    {
        LogMessage(LogLevel::Error, "material",
                   "CreateMaterial needs either ShaderPath or Bytecode.");
        return id;
    }

    SDL_GPUGraphicsPipeline* pipeline =
        fromFile ? BuildPipeline(compiled, desc.EntryPoint) : BuildPipeline(desc.Bytecode, desc.EntryPoint);
    if (pipeline == nullptr)
    {
        LogMessage(LogLevel::Error, "material",
                   "Could not build a pipeline for the material. When its Bytecode comes from "
                   "opane_add_material, check that the build compiled it to %s, the format this "
                   "device's backend takes.",
                   ShaderFormatName(DeviceShaderFormat(m_Renderer->GetDevice())));
        return id;
    }

    uint32_t index;
    if (!m_FreeMaterials.empty())
    {
        index = m_FreeMaterials.back();
        m_FreeMaterials.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_Materials.size());
        m_Materials.emplace_back();
    }

    MaterialRecord& record = m_Materials[index];
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }

    record.Alive = true;
    record.Pipeline = pipeline;
    record.EntryPoint = desc.EntryPoint;
    record.SourcePath = shaderPath;
    record.HotReload = fromFile && desc.HotReload;
    record.SourceTimestamp = fromFile ? GetTimestamp(shaderPath) : 0;

    record.UniformNames.clear();
    record.Uniforms = MaterialUniformBlock{};

    for (size_t slot = 0; slot < desc.Uniforms.size(); ++slot)
    {
        const MaterialUniform& uniform = desc.Uniforms[slot];
        record.UniformNames.push_back(uniform.Name);
        record.Uniforms.Params[slot][0] = uniform.X;
        record.Uniforms.Params[slot][1] = uniform.Y;
        record.Uniforms.Params[slot][2] = uniform.Z;
        record.Uniforms.Params[slot][3] = uniform.W;
    }

    if (fromFile)
    {
        LogMessage(LogLevel::Info, "material", "Compiled \"%s\"%s.", shaderPath.c_str(),
                   record.HotReload ? " with hot reload" : "");
    }

    id.Index = index;
    id.Generation = record.Generation;
    return id;
}

void MaterialStore::Destroy(MaterialId id)
{
    MaterialRecord* record = Resolve(id);
    if (record == nullptr)
    {
        return;
    }

    if (record->Pipeline != nullptr && m_Renderer != nullptr)
    {
        m_Renderer->ReleasePipeline(record->Pipeline);
    }

    record->Pipeline = nullptr;
    record->Alive = false;
    record->UniformNames.clear();
    record->SourcePath.clear();

    ++record->Generation;
    if (record->Generation == 0)
    {
        record->Generation = 1;
    }

    m_FreeMaterials.push_back(id.Index);
}

MaterialRecord* MaterialStore::Resolve(const MaterialId& id)
{
    if (!id.IsValid() || id.Index >= m_Materials.size())
    {
        return nullptr;
    }

    MaterialRecord& record = m_Materials[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        return nullptr;
    }
    return &record;
}

const MaterialRecord* MaterialStore::Resolve(const MaterialId& id) const
{
    return const_cast<MaterialStore*>(this)->Resolve(id);
}

namespace
{

} // namespace

// The shape of the movement between two values. Smooth starts and stops
// gently.
float ApplyEasing(Easing easing, float t)
{
    switch (easing)
    {
        case Easing::Linear: return t;
        case Easing::In: return t * t;
        case Easing::Out: return 1.0f - (1.0f - t) * (1.0f - t);
        case Easing::Smooth:
        default: return t * t * (3.0f - 2.0f * t);
    }
}

void MaterialStore::Animate(const MaterialId& id, const std::string& name, const float target[4],
                            float seconds, Easing easing)
{
    MaterialRecord* record = Resolve(id);
    if (record == nullptr)
    {
        LogMessage(LogLevel::Warning, "material",
                   "AnimateMaterialUniform was called on a material that no longer exists.");
        return;
    }

    const auto slot = std::find(record->UniformNames.begin(), record->UniformNames.end(), name);
    if (slot == record->UniformNames.end())
    {
        LogMessage(LogLevel::Warning, "material",
                   "\"%s\" is not a uniform of this material. Declare it in MaterialDesc::Uniforms.",
                   name.c_str());
        return;
    }

    const size_t index = static_cast<size_t>(std::distance(record->UniformNames.begin(), slot));

    // Anything already moving this uniform gives way, from where it is now.
    for (size_t existing = 0; existing < m_Animations.size();)
    {
        if (m_Animations[existing].Material == id && m_Animations[existing].Slot == index)
        {
            m_Animations.erase(m_Animations.begin() + static_cast<ptrdiff_t>(existing));
            continue;
        }
        ++existing;
    }

    if (!(seconds > 0.0f))
    {
        for (int component = 0; component < 4; ++component)
        {
            record->Uniforms.Params[index][component] = target[component];
        }
        return;
    }

    UniformAnimation animation;
    animation.Material = id;
    animation.Slot = index;
    animation.Duration = seconds;
    animation.Curve = easing;
    for (int component = 0; component < 4; ++component)
    {
        animation.From[component] = record->Uniforms.Params[index][component];
        animation.To[component] = target[component];
    }
    m_Animations.push_back(animation);
}

void MaterialStore::CancelAnimations(const MaterialId& id)
{
    for (size_t index = 0; index < m_Animations.size();)
    {
        if (m_Animations[index].Material == id)
        {
            m_Animations.erase(m_Animations.begin() + static_cast<ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
}

void MaterialStore::SetUniform(const MaterialId& id, const std::string& name, float x, float y,
                               float z, float w)
{
    MaterialRecord* record = Resolve(id);
    if (record == nullptr)
    {
        LogMessage(LogLevel::Warning, "material",
                   "SetMaterialUniform was called on a material that no longer exists.");
        return;
    }

    const auto slot = std::find(record->UniformNames.begin(), record->UniformNames.end(), name);
    if (slot == record->UniformNames.end())
    {
        LogMessage(LogLevel::Warning, "material",
                   "\"%s\" is not a uniform of this material. Declare it in MaterialDesc::Uniforms.",
                   name.c_str());
        return;
    }

    const size_t index = static_cast<size_t>(std::distance(record->UniformNames.begin(), slot));
    record->Uniforms.Params[index][0] = x;
    record->Uniforms.Params[index][1] = y;
    record->Uniforms.Params[index][2] = z;
    record->Uniforms.Params[index][3] = w;
}

void MaterialStore::Tick(float totalSeconds, float deltaSeconds)
{
    m_TotalSeconds = totalSeconds;
    m_DeltaSeconds = deltaSeconds;
    m_ReloadTimer += deltaSeconds;

    for (MaterialRecord& record : m_Materials)
    {
        if (record.Alive)
        {
            record.Uniforms.Time[0] = totalSeconds;
            record.Uniforms.Time[1] = deltaSeconds;
        }
    }

    for (size_t index = 0; index < m_Animations.size();)
    {
        UniformAnimation& animation = m_Animations[index];
        MaterialRecord* record = Resolve(animation.Material);
        if (record == nullptr)
        {
            // The material went away while its value was still moving.
            m_Animations.erase(m_Animations.begin() + static_cast<ptrdiff_t>(index));
            continue;
        }

        animation.Elapsed += deltaSeconds;
        const float linear = std::min(1.0f, animation.Elapsed / animation.Duration);
        const float eased = ApplyEasing(animation.Curve, linear);

        for (int component = 0; component < 4; ++component)
        {
            record->Uniforms.Params[animation.Slot][component] =
                animation.From[component] + (animation.To[component] - animation.From[component]) * eased;
        }

        if (linear >= 1.0f)
        {
            m_Animations.erase(m_Animations.begin() + static_cast<ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
}

void MaterialStore::ReloadChanged()
{
    if (m_ReloadTimer < ReloadInterval)
    {
        return;
    }
    m_ReloadTimer = 0.0f;

    for (MaterialRecord& record : m_Materials)
    {
        if (!record.Alive || !record.HotReload || record.SourcePath.empty())
        {
            continue;
        }

        const int64_t timestamp = GetTimestamp(record.SourcePath);
        if (timestamp == 0 || timestamp == record.SourceTimestamp)
        {
            continue;
        }

        record.SourceTimestamp = timestamp;

        std::vector<unsigned char> compiled;
        std::string error;
        if (!CompileToBytecode(record.SourcePath, record.EntryPoint, compiled, error))
        {
            // The previous pipeline stays bound, so a typo costs a diagnostic
            // rather than a black screen.
            LogMessage(LogLevel::Error, "material", "Reload of \"%s\" failed:\n%s",
                       record.SourcePath.c_str(), error.c_str());
            continue;
        }

        SDL_GPUGraphicsPipeline* pipeline = BuildPipeline(compiled, record.EntryPoint);
        if (pipeline == nullptr)
        {
            LogMessage(LogLevel::Error, "material",
                       "Reload of \"%s\" compiled but produced no pipeline.",
                       record.SourcePath.c_str());
            continue;
        }

        if (record.Pipeline != nullptr && m_Renderer != nullptr)
        {
            m_Renderer->ReleasePipeline(record.Pipeline);
        }
        record.Pipeline = pipeline;

        // Uniform values survive, so a reload does not reset what the program
        // has set.
        LogMessage(LogLevel::Info, "material", "Reloaded \"%s\".", record.SourcePath.c_str());
    }
}

} // namespace detail
} // namespace opane
