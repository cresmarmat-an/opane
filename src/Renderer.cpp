#include "Renderer.h"

#include "Backend.h"

#include <algorithm>
#include <cstring>

namespace opane
{
namespace
{

constexpr size_t InitialVertexCapacity = 4096;
constexpr size_t InitialIndexCapacity = 6144;

// The shader in whichever format the device takes: DXIL, SPIR-V, or MSL.
SDL_GPUShader* CreateShader(SDL_GPUDevice* device, SDL_GPUShaderStage stage, const char* entryPoint,
                            const ShaderBytecode& bytecode, uint32_t samplerCount, uint32_t uniformBufferCount)
{
    const SDL_GPUShaderFormat format = detail::DeviceShaderFormat(device);
    const void* code = nullptr;
    size_t codeSize = 0;
    if (!detail::PickShaderCode(bytecode, format, code, codeSize))
    {
        LogMessage(LogLevel::Error, "gpu", "The %s shader was not compiled to %s, which this device's backend takes.",
                   entryPoint, detail::ShaderFormatName(format));
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = codeSize;
    info.entrypoint = entryPoint;
    info.format = format;
    info.stage = stage;
    info.num_samplers = samplerCount;
    info.num_storage_textures = 0;
    info.num_storage_buffers = 0;
    info.num_uniform_buffers = uniformBufferCount;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (shader == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not create the %s shader: %s", entryPoint,
                   SDL_GetError());
    }
    return shader;
}

} // namespace

SDL_GPUGraphicsPipeline* Renderer::BuildPipeline(SDL_GPUShader* fragmentShader) const
{
    if (m_Device == nullptr || m_VertexShader == nullptr || fragmentShader == nullptr)
    {
        return nullptr;
    }

    SDL_GPUVertexBufferDescription bufferDescription{};
    bufferDescription.slot = 0;
    bufferDescription.pitch = sizeof(DrawList::Vertex);
    bufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    bufferDescription.instance_step_rate = 0;

    SDL_GPUVertexAttribute attributes[6]{};
    attributes[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0 };
    attributes[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, sizeof(float) * 2 };
    attributes[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, sizeof(float) * 4 };
    attributes[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, sizeof(float) * 8 };
    attributes[4] = { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, sizeof(float) * 12 };
    attributes[5] = { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, sizeof(float) * 16 };

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = m_SwapchainFormat;
    colorTarget.blend_state.enable_blend = true;
    colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    // Shaders write premultiplied alpha, which is what makes overlapping
    // translucent elements and anti-aliased glyph edges composite correctly.
    colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = m_VertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &bufferDescription;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.vertex_attributes = attributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 6;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.has_depth_stencil_target = false;

    return SDL_CreateGPUGraphicsPipeline(m_Device, &pipelineInfo);
}

SDL_GPUGraphicsPipeline* Renderer::CreatePipelineForFragmentShader(const ShaderBytecode& bytecode,
                                                                   const char* entryPoint) const
{
    // A material declares one sampler and one uniform buffer, matching the
    // bindings material.hlsli documents.
    SDL_GPUShader* fragmentShader =
        CreateShader(m_Device, SDL_GPU_SHADERSTAGE_FRAGMENT, entryPoint, bytecode, 1, 1);
    if (fragmentShader == nullptr)
    {
        return nullptr;
    }

    SDL_GPUGraphicsPipeline* pipeline = BuildPipeline(fragmentShader);
    SDL_ReleaseGPUShader(m_Device, fragmentShader);

    if (pipeline == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not build a material pipeline: %s",
                   SDL_GetError());
    }
    return pipeline;
}

void Renderer::ReleasePipeline(SDL_GPUGraphicsPipeline* pipeline) const
{
    if (m_Device != nullptr && pipeline != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_Device, pipeline);
    }
}

bool Renderer::Initialize(SDL_GPUDevice* device, SDL_Window* window)
{
    m_Device = device;

    SDL_GPUShader* vertexShader =
        CreateShader(device, SDL_GPU_SHADERSTAGE_VERTEX, "VertexMain", detail::UiVertexShader(), 0, 1);
    SDL_GPUShader* fragmentShader =
        CreateShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT, "FragmentMain", detail::UiFragmentShader(), 1, 0);

    if (vertexShader == nullptr || fragmentShader == nullptr)
    {
        return false;
    }

    // The vertex shader is kept for the lifetime of the renderer, because every
    // material pairs its own fragment stage with this one.
    m_VertexShader = vertexShader;
    m_SwapchainFormat = SDL_GetGPUSwapchainTextureFormat(device, window);

    m_Pipeline = BuildPipeline(fragmentShader);

    SDL_ReleaseGPUShader(device, fragmentShader);

    if (m_Pipeline == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not create the interface pipeline: %s",
                   SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    // Trilinear with 8x anisotropy. An image drawn smaller than its size
    // samples a pre-filtered mip level instead of skipping texels, so it does
    // not shimmer.
    samplerInfo.min_lod = 0.0f;
    samplerInfo.max_lod = 1000.0f;
    samplerInfo.enable_anisotropy = true;
    samplerInfo.max_anisotropy = 8.0f;

    m_Sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (m_Sampler == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not create the interface sampler: %s",
                   SDL_GetError());
        return false;
    }

    if (!EnsureBufferCapacity(InitialVertexCapacity, InitialIndexCapacity))
    {
        return false;
    }

    // Untextured shapes still sample a texture, so a single white pixel keeps
    // every primitive on one pipeline and inside one batch.
    const uint32_t whitePixel = 0xFFFFFFFFu;
    m_WhiteTexture = CreateTexture(1, 1, PixelFormat::Rgba8, &whitePixel);

    // Frosted glass needs its own pipeline; without it the rest still draws.
    CreateBlurPipeline();

    return m_WhiteTexture.IsValid();
}

void Renderer::Shutdown()
{
    if (m_Device == nullptr)
    {
        return;
    }

    for (TextureRecord& record : m_Textures)
    {
        if (record.Alive && record.Owned && record.Texture != nullptr)
        {
            SDL_ReleaseGPUTexture(m_Device, record.Texture);
        }
    }
    m_Textures.clear();
    m_FreeTextures.clear();
    m_Gradients.clear();

    ReleaseBlurTextures();
    if (m_BlurPipeline != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_Device, m_BlurPipeline);
        m_BlurPipeline = nullptr;
    }
    if (m_ClampSampler != nullptr)
    {
        SDL_ReleaseGPUSampler(m_Device, m_ClampSampler);
        m_ClampSampler = nullptr;
    }

    if (m_TransferBuffer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(m_Device, m_TransferBuffer);
        m_TransferBuffer = nullptr;
    }
    if (m_VertexBuffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(m_Device, m_VertexBuffer);
        m_VertexBuffer = nullptr;
    }
    if (m_IndexBuffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(m_Device, m_IndexBuffer);
        m_IndexBuffer = nullptr;
    }
    if (m_Sampler != nullptr)
    {
        SDL_ReleaseGPUSampler(m_Device, m_Sampler);
        m_Sampler = nullptr;
    }
    if (m_Pipeline != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_Device, m_Pipeline);
        m_Pipeline = nullptr;
    }
    if (m_VertexShader != nullptr)
    {
        SDL_ReleaseGPUShader(m_Device, m_VertexShader);
        m_VertexShader = nullptr;
    }

    m_Device = nullptr;
}

bool Renderer::EnsureBufferCapacity(size_t vertexCount, size_t indexCount)
{
    if (vertexCount <= m_VertexCapacity && indexCount <= m_IndexCapacity &&
        m_VertexBuffer != nullptr)
    {
        return true;
    }

    // Grow geometrically so a scene that keeps adding elements stops
    // reallocating quickly rather than once per frame.
    size_t newVertexCapacity = std::max(m_VertexCapacity, InitialVertexCapacity);
    while (newVertexCapacity < vertexCount)
    {
        newVertexCapacity *= 2;
    }

    size_t newIndexCapacity = std::max(m_IndexCapacity, InitialIndexCapacity);
    while (newIndexCapacity < indexCount)
    {
        newIndexCapacity *= 2;
    }

    if (m_VertexBuffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(m_Device, m_VertexBuffer);
    }
    if (m_IndexBuffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(m_Device, m_IndexBuffer);
    }
    if (m_TransferBuffer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(m_Device, m_TransferBuffer);
    }

    const size_t vertexBytes = newVertexCapacity * sizeof(DrawList::Vertex);
    const size_t indexBytes = newIndexCapacity * sizeof(uint32_t);

    SDL_GPUBufferCreateInfo vertexInfo{};
    vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertexInfo.size = static_cast<uint32_t>(vertexBytes);
    m_VertexBuffer = SDL_CreateGPUBuffer(m_Device, &vertexInfo);

    SDL_GPUBufferCreateInfo indexInfo{};
    indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    indexInfo.size = static_cast<uint32_t>(indexBytes);
    m_IndexBuffer = SDL_CreateGPUBuffer(m_Device, &indexInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<uint32_t>(vertexBytes + indexBytes);
    m_TransferBuffer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);

    if (m_VertexBuffer == nullptr || m_IndexBuffer == nullptr || m_TransferBuffer == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not allocate interface geometry buffers: %s",
                   SDL_GetError());
        return false;
    }

    m_VertexCapacity = newVertexCapacity;
    m_IndexCapacity = newIndexCapacity;
    return true;
}

TextureId Renderer::CreateTexture(int width, int height, PixelFormat format, const void* pixels)
{
    if (pixels == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "CreateTexture was given no pixels.");
        return TextureId{};
    }

    const std::vector<const void*> levels{ pixels };
    return CreateTextureLevels(width, height, format, levels);
}

bool Renderer::UpdateTexture(TextureId id, int width, int height, PixelFormat format,
                             const std::vector<const void*>& levels)
{
    if (m_Device == nullptr || levels.empty() || levels[0] == nullptr || width <= 0 || height <= 0)
    {
        return false;
    }

    if (!id.IsValid() || id.Index >= m_Textures.size())
    {
        return false;
    }

    TextureRecord& record = m_Textures[id.Index];
    if (!record.Alive || record.Generation != id.Generation || !record.Owned)
    {
        return false;
    }

    const bool isAlpha = (format == PixelFormat::Alpha8);
    const uint32_t bytesPerPixel = isAlpha ? 1u : 4u;
    const uint32_t levelCount = static_cast<uint32_t>(levels.size());

    // A different size, or a different number of levels, needs a different
    // texture. Releasing the old one is safe while a frame still refers to it:
    // SDL frees it once the GPU has finished with it, and the handle here
    // keeps pointing at the new one.
    if (record.Width != width || record.Height != height || record.Levels != levelCount)
    {
        SDL_GPUTextureCreateInfo textureInfo{};
        textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
        textureInfo.format =
            isAlpha ? SDL_GPU_TEXTUREFORMAT_R8_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        textureInfo.width = static_cast<uint32_t>(width);
        textureInfo.height = static_cast<uint32_t>(height);
        textureInfo.layer_count_or_depth = 1;
        textureInfo.num_levels = levelCount;
        textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        SDL_GPUTexture* replacement = SDL_CreateGPUTexture(m_Device, &textureInfo);
        if (replacement == nullptr)
        {
            LogMessage(LogLevel::Error, "gpu", "Could not resize a texture: %s", SDL_GetError());
            return false;
        }

        SDL_ReleaseGPUTexture(m_Device, record.Texture);
        record.Texture = replacement;
        record.Width = width;
        record.Height = height;
        record.Levels = levelCount;
    }

    std::vector<uint32_t> offsets(levelCount);
    uint32_t totalBytes = 0;
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const uint32_t levelWidth = std::max(1u, static_cast<uint32_t>(width) >> level);
        const uint32_t levelHeight = std::max(1u, static_cast<uint32_t>(height) >> level);
        offsets[level] = totalBytes;
        totalBytes += levelWidth * levelHeight * bytesPerPixel;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = totalBytes;

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
    if (transfer == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not stage texture pixels: %s", SDL_GetError());
        return false;
    }

    auto* mapped = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(m_Device, transfer, false));
    if (mapped != nullptr)
    {
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const uint32_t levelWidth = std::max(1u, static_cast<uint32_t>(width) >> level);
            const uint32_t levelHeight = std::max(1u, static_cast<uint32_t>(height) >> level);
            std::memcpy(mapped + offsets[level], levels[level], levelWidth * levelHeight * bytesPerPixel);
        }
        SDL_UnmapGPUTransferBuffer(m_Device, transfer);
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_Device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const uint32_t levelWidth = std::max(1u, static_cast<uint32_t>(width) >> level);
        const uint32_t levelHeight = std::max(1u, static_cast<uint32_t>(height) >> level);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.offset = offsets[level];
        source.pixels_per_row = levelWidth;
        source.rows_per_layer = levelHeight;

        SDL_GPUTextureRegion destination{};
        destination.texture = record.Texture;
        destination.mip_level = level;
        destination.w = levelWidth;
        destination.h = levelHeight;
        destination.d = 1;

        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_ReleaseGPUTransferBuffer(m_Device, transfer);

    return true;
}

TextureId Renderer::CreateTextureLevels(int width, int height, PixelFormat format,
                                        const std::vector<const void*>& levels)
{
    TextureId id;

    if (m_Device == nullptr || width <= 0 || height <= 0 || levels.empty() || levels[0] == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "CreateTexture was given invalid dimensions or no pixels.");
        return id;
    }

    const bool isAlpha = (format == PixelFormat::Alpha8);
    const uint32_t bytesPerPixel = isAlpha ? 1u : 4u;
    const uint32_t levelCount = static_cast<uint32_t>(levels.size());

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = isAlpha ? SDL_GPU_TEXTUREFORMAT_R8_UNORM : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = static_cast<uint32_t>(width);
    textureInfo.height = static_cast<uint32_t>(height);
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = levelCount;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(m_Device, &textureInfo);
    if (texture == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not create a texture: %s", SDL_GetError());
        return id;
    }

    // Every level goes through one staging buffer, laid end to end.
    std::vector<uint32_t> offsets(levelCount);
    uint32_t totalBytes = 0;
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const uint32_t levelWidth = std::max(1u, static_cast<uint32_t>(width) >> level);
        const uint32_t levelHeight = std::max(1u, static_cast<uint32_t>(height) >> level);
        offsets[level] = totalBytes;
        totalBytes += levelWidth * levelHeight * bytesPerPixel;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = totalBytes;

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_Device, &transferInfo);
    if (transfer == nullptr)
    {
        SDL_ReleaseGPUTexture(m_Device, texture);
        LogMessage(LogLevel::Error, "gpu", "Could not stage texture pixels: %s", SDL_GetError());
        return id;
    }

    auto* mapped = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(m_Device, transfer, false));
    if (mapped != nullptr)
    {
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const uint32_t levelWidth = std::max(1u, static_cast<uint32_t>(width) >> level);
            const uint32_t levelHeight = std::max(1u, static_cast<uint32_t>(height) >> level);
            std::memcpy(mapped + offsets[level], levels[level], levelWidth * levelHeight * bytesPerPixel);
        }
        SDL_UnmapGPUTransferBuffer(m_Device, transfer);
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_Device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const uint32_t levelWidth = std::max(1u, static_cast<uint32_t>(width) >> level);
        const uint32_t levelHeight = std::max(1u, static_cast<uint32_t>(height) >> level);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.offset = offsets[level];
        source.pixels_per_row = levelWidth;
        source.rows_per_layer = levelHeight;

        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.mip_level = level;
        destination.w = levelWidth;
        destination.h = levelHeight;
        destination.d = 1;

        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(commandBuffer);

    SDL_ReleaseGPUTransferBuffer(m_Device, transfer);

    uint32_t index;
    if (!m_FreeTextures.empty())
    {
        index = m_FreeTextures.back();
        m_FreeTextures.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_Textures.size());
        m_Textures.emplace_back();
    }

    TextureRecord& record = m_Textures[index];
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }
    record.Alive = true;
    record.Owned = true;
    record.Texture = texture;
    record.Width = width;
    record.Height = height;
    record.Levels = levelCount;

    id.Index = index;
    id.Generation = record.Generation;
    return id;
}

TextureId Renderer::AdoptTexture(SDL_GPUTexture* texture, int width, int height)
{
    TextureId id;

    if (m_Device == nullptr || texture == nullptr || width <= 0 || height <= 0)
    {
        return id;
    }

    uint32_t index;
    if (!m_FreeTextures.empty())
    {
        index = m_FreeTextures.back();
        m_FreeTextures.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(m_Textures.size());
        m_Textures.emplace_back();
    }

    TextureRecord& record = m_Textures[index];
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }

    record.Alive = true;
    record.Owned = false;
    record.Texture = texture;
    record.Width = width;
    record.Height = height;

    id.Index = index;
    id.Generation = record.Generation;
    return id;
}

void Renderer::DestroyTexture(TextureId id)
{
    if (!id.IsValid() || id.Index >= m_Textures.size())
    {
        return;
    }

    TextureRecord& record = m_Textures[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        LogMessage(LogLevel::Warning, "gpu", "DestroyTexture was called on a texture that is already gone.");
        return;
    }

    // A texture adopted from elsewhere belongs to whoever created it, so only
    // the handle is reclaimed here.
    if (record.Owned)
    {
        SDL_ReleaseGPUTexture(m_Device, record.Texture);
    }

    record.Texture = nullptr;
    record.Alive = false;
    ++record.Generation;
    if (record.Generation == 0)
    {
        record.Generation = 1;
    }
    m_FreeTextures.push_back(id.Index);
}

SDL_GPUTexture* Renderer::ResolveTexture(const TextureId& id) const
{
    if (!id.IsValid() || id.Index >= m_Textures.size())
    {
        return nullptr;
    }

    const TextureRecord& record = m_Textures[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        return nullptr;
    }
    return record.Texture;
}

Vec2 Renderer::GetTextureSize(TextureId id) const
{
    if (!id.IsValid() || id.Index >= m_Textures.size())
    {
        return Vec2{};
    }

    const TextureRecord& record = m_Textures[id.Index];
    if (!record.Alive || record.Generation != id.Generation)
    {
        return Vec2{};
    }
    return Vec2{ static_cast<float>(record.Width), static_cast<float>(record.Height) };
}

void Renderer::Upload(SDL_GPUCommandBuffer* commandBuffer, const DrawList& drawList)
{
    m_UploadedIndexCount = 0;
    ++m_UploadCount;

    const size_t vertexCount = drawList.m_Vertices.size();
    const size_t indexCount = drawList.m_Indices.size();
    if (vertexCount == 0 || indexCount == 0)
    {
        return;
    }

    if (!EnsureBufferCapacity(vertexCount, indexCount))
    {
        return;
    }

    const size_t vertexBytes = vertexCount * sizeof(DrawList::Vertex);
    const size_t indexBytes = indexCount * sizeof(uint32_t);

    // cycle = true hands back fresh storage instead of stalling on the frame
    // still in flight, so the CPU does not wait on the GPU.
    void* mapped = SDL_MapGPUTransferBuffer(m_Device, m_TransferBuffer, true);
    if (mapped == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not map the interface transfer buffer: %s",
                   SDL_GetError());
        return;
    }

    std::memcpy(mapped, drawList.m_Vertices.data(), vertexBytes);
    std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes, drawList.m_Indices.data(), indexBytes);
    SDL_UnmapGPUTransferBuffer(m_Device, m_TransferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = m_TransferBuffer;
    source.offset = 0;

    SDL_GPUBufferRegion vertexRegion{};
    vertexRegion.buffer = m_VertexBuffer;
    vertexRegion.offset = 0;
    vertexRegion.size = static_cast<uint32_t>(vertexBytes);
    SDL_UploadToGPUBuffer(copyPass, &source, &vertexRegion, true);

    source.offset = static_cast<uint32_t>(vertexBytes);

    SDL_GPUBufferRegion indexRegion{};
    indexRegion.buffer = m_IndexBuffer;
    indexRegion.offset = 0;
    indexRegion.size = static_cast<uint32_t>(indexBytes);
    SDL_UploadToGPUBuffer(copyPass, &source, &indexRegion, true);

    SDL_EndGPUCopyPass(copyPass);

    m_UploadedIndexCount = static_cast<uint32_t>(indexCount);
}

TextureId Renderer::CreateRenderTarget(int width, int height)
{
    TextureId id;

    if (m_Device == nullptr || width <= 0 || height <= 0)
    {
        return id;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = m_SwapchainFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = static_cast<uint32_t>(width);
    info.height = static_cast<uint32_t>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(m_Device, &info);
    if (texture == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not create a render target: %s", SDL_GetError());
        return id;
    }

    // Registered as owned, so DestroyTexture releases it like any other.
    id = AdoptTexture(texture, width, height);
    if (id.IsValid())
    {
        m_Textures[id.Index].Owned = true;
    }
    return id;
}

bool Renderer::NeedsSampledTarget(const DrawList& drawList) const
{
    for (const DrawList::Batch& batch : drawList.m_Batches)
    {
        if (batch.BackdropBlur > 0.0f && batch.IndexCount > 0)
        {
            return true;
        }
    }
    return false;
}

void Renderer::Render(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass*& renderPass, SDL_GPUTexture* target,
                      const DrawList& drawList, int viewportWidth, int viewportHeight,
                      detail::MaterialStore* materials, Vec2 origin)
{
    m_LastDrawCalls = 0;

    if (renderPass == nullptr || m_UploadedIndexCount == 0 || drawList.m_Batches.empty())
    {
        return;
    }

    SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
    const float uniforms[4] = { static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
                                origin.X, origin.Y };

    // Everything a pass needs before its first draw, again after a backdrop
    // has interrupted it.
    auto BindGeometry = [&] {
        SDL_GPUBufferBinding vertexBinding{};
        vertexBinding.buffer = m_VertexBuffer;
        vertexBinding.offset = 0;
        SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);

        SDL_GPUBufferBinding indexBinding{};
        indexBinding.buffer = m_IndexBuffer;
        indexBinding.offset = 0;
        SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_PushGPUVertexUniformData(commandBuffer, 0, uniforms, sizeof(uniforms));
        boundPipeline = nullptr;
    };
    BindGeometry();

    for (const DrawList::Batch& batch : drawList.m_Batches)
    {
        if (batch.IndexCount == 0)
        {
            continue;
        }

        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = m_Sampler;

        if (batch.BackdropBlur > 0.0f)
        {
            // Frosted glass: the pass ends so what it has drawn so far can be
            // read, blurred, and shown through the glass, then carries on.
            if (target == nullptr || m_BlurPipeline == nullptr)
            {
                if (!m_BlurUnavailableReported)
                {
                    m_BlurUnavailableReported = true;
                    LogMessage(LogLevel::Warning, "gpu",
                               "Frosted glass (BackdropBlur) is drawn without its blur: %s",
                               m_BlurPipeline == nullptr ? "the blur pipeline could not be made"
                                                         : "this target cannot be read back");
                }
                continue;
            }

            SDL_EndGPURenderPass(renderPass);
            renderPass = nullptr;

            texture = BlurBackdrop(commandBuffer, target, viewportWidth, viewportHeight, batch.BackdropBlur);
            sampler = m_ClampSampler;

            SDL_GPUColorTargetInfo resume{};
            resume.texture = target;
            resume.load_op = SDL_GPU_LOADOP_LOAD;
            resume.store_op = SDL_GPU_STOREOP_STORE;
            renderPass = SDL_BeginGPURenderPass(commandBuffer, &resume, 1, nullptr);
            if (renderPass == nullptr)
            {
                LogMessage(LogLevel::Error, "gpu", "Could not resume the pass after a backdrop blur: %s",
                           SDL_GetError());
                return;
            }
            BindGeometry();
            if (texture == nullptr)
            {
                continue;
            }
        }

        // A batch carrying a material swaps in that material's pipeline and
        // pushes its uniforms. Everything else uses the default pipeline, and
        // consecutive batches sharing one avoid a redundant bind.
        SDL_GPUGraphicsPipeline* pipeline = m_Pipeline;
        const detail::MaterialRecord* material =
            materials != nullptr ? materials->Resolve(batch.Material) : nullptr;

        if (material != nullptr && material->Pipeline != nullptr)
        {
            pipeline = material->Pipeline;
        }

        if (pipeline != boundPipeline)
        {
            SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
            boundPipeline = pipeline;

            // Re-pushed after every pipeline change so the vertex stage always
            // has the viewport it needs to map pixels to clip space.
            SDL_PushGPUVertexUniformData(commandBuffer, 0, uniforms, sizeof(uniforms));
        }

        if (material != nullptr)
        {
            SDL_PushGPUFragmentUniformData(commandBuffer, 0, &material->Uniforms,
                                           sizeof(detail::MaterialUniformBlock));
        }

        if (texture == nullptr)
        {
            texture = ResolveTexture(batch.Texture);
        }
        if (texture == nullptr)
        {
            // A destroyed or absent texture falls back to white rather than
            // dropping the geometry, so a missing image is visible instead of
            // silently blank.
            texture = ResolveTexture(m_WhiteTexture);
        }
        if (texture == nullptr)
        {
            continue;
        }

        // Clip rectangles are recorded in window pixels; a render target sits
        // at an offset, so they are moved into the target's own space first.
        const float clipX = batch.Clip.X - origin.X;
        const float clipY = batch.Clip.Y - origin.Y;

        const int left = std::max(0, static_cast<int>(clipX));
        const int top = std::max(0, static_cast<int>(clipY));
        const int right = std::min(viewportWidth, static_cast<int>(clipX + batch.Clip.Width + 0.5f));
        const int bottom = std::min(viewportHeight, static_cast<int>(clipY + batch.Clip.Height + 0.5f));

        if (right <= left || bottom <= top)
        {
            continue;
        }

        SDL_Rect scissor{ left, top, right - left, bottom - top };
        SDL_SetGPUScissor(renderPass, &scissor);

        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = texture;
        samplerBinding.sampler = sampler;
        SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);

        SDL_DrawGPUIndexedPrimitives(renderPass, batch.IndexCount, 1, batch.FirstIndex, 0, 0);
        ++m_LastDrawCalls;
    }
}

// --- gradients ------------------------------------------------------------------

TextureId Renderer::GetGradientTexture(const std::vector<GradientStop>& stops)
{
    if (stops.empty() || m_Device == nullptr)
    {
        return TextureId{};
    }

    // Keyed by what can be told apart in a byte-per-channel strip.
    std::string key;
    auto Byte = [](float value) { return static_cast<char>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    for (const GradientStop& stop : stops)
    {
        key += Byte(stop.Position);
        key += Byte(stop.Color.R);
        key += Byte(stop.Color.G);
        key += Byte(stop.Color.B);
        key += Byte(stop.Color.A);
    }

    if (auto found = m_Gradients.find(key); found != m_Gradients.end())
    {
        found->second.LastUsed = m_UploadCount;
        return found->second.Texture;
    }

    // 256 steps, premultiplied, so filtering between two of them blends
    // colour and coverage together as a gradient to transparent needs.
    constexpr int Width = 256;
    uint8_t pixels[Width * 4];
    for (int index = 0; index < Width; ++index)
    {
        const float t = static_cast<float>(index) / static_cast<float>(Width - 1);
        Color color = stops.front().Color;
        if (t > stops.front().Position)
        {
            color = stops.back().Color;
            for (size_t stop = 1; stop < stops.size(); ++stop)
            {
                if (t <= stops[stop].Position)
                {
                    const float span = stops[stop].Position - stops[stop - 1].Position;
                    const float along = span > 1e-6f ? (t - stops[stop - 1].Position) / span : 1.0f;
                    const Color& a = stops[stop - 1].Color;
                    const Color& b = stops[stop].Color;
                    color = Color{ a.R + (b.R - a.R) * along, a.G + (b.G - a.G) * along,
                                   a.B + (b.B - a.B) * along, a.A + (b.A - a.A) * along };
                    break;
                }
            }
        }
        const float alpha = std::clamp(color.A, 0.0f, 1.0f);
        pixels[index * 4 + 0] = static_cast<uint8_t>(std::clamp(color.R, 0.0f, 1.0f) * alpha * 255.0f + 0.5f);
        pixels[index * 4 + 1] = static_cast<uint8_t>(std::clamp(color.G, 0.0f, 1.0f) * alpha * 255.0f + 0.5f);
        pixels[index * 4 + 2] = static_cast<uint8_t>(std::clamp(color.B, 0.0f, 1.0f) * alpha * 255.0f + 0.5f);
        pixels[index * 4 + 3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
    }

    // A gradient eased between two states is a new gradient every frame, so
    // the cache is bounded: when it is full, the least recently used strip
    // (one not used this frame) is rewritten for the new one.
    constexpr size_t Capacity = 256;
    if (m_Gradients.size() >= Capacity)
    {
        auto oldest = m_Gradients.end();
        for (auto entry = m_Gradients.begin(); entry != m_Gradients.end(); ++entry)
        {
            if (entry->second.LastUsed < m_UploadCount &&
                (oldest == m_Gradients.end() || entry->second.LastUsed < oldest->second.LastUsed))
            {
                oldest = entry;
            }
        }
        if (oldest != m_Gradients.end())
        {
            const TextureId recycled = oldest->second.Texture;
            m_Gradients.erase(oldest);
            if (UpdateTexture(recycled, Width, 1, PixelFormat::Rgba8, { pixels }))
            {
                m_Gradients.emplace(key, GradientRecord{ recycled, m_UploadCount });
                return recycled;
            }
            DestroyTexture(recycled);
        }
    }

    const TextureId texture = CreateTexture(Width, 1, PixelFormat::Rgba8, pixels);
    if (texture.IsValid())
    {
        m_Gradients.emplace(key, GradientRecord{ texture, m_UploadCount });
    }
    return texture;
}

// --- frosted glass --------------------------------------------------------------

bool Renderer::CreateBlurPipeline()
{
    SDL_GPUShader* vertex =
        CreateShader(m_Device, SDL_GPU_SHADERSTAGE_VERTEX, "BlurVertexMain", detail::UiBlurVertexShader(), 0, 0);
    SDL_GPUShader* fragment = CreateShader(m_Device, SDL_GPU_SHADERSTAGE_FRAGMENT, "BlurFragmentMain",
                                           detail::UiBlurFragmentShader(), 1, 1);
    if (vertex != nullptr && fragment != nullptr)
    {
        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = m_SwapchainFormat;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex;
        info.fragment_shader = fragment;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions = &colorTarget;
        info.target_info.num_color_targets = 1;
        m_BlurPipeline = SDL_CreateGPUGraphicsPipeline(m_Device, &info);
    }
    if (vertex != nullptr)
    {
        SDL_ReleaseGPUShader(m_Device, vertex);
    }
    if (fragment != nullptr)
    {
        SDL_ReleaseGPUShader(m_Device, fragment);
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.max_lod = 1000.0f;
    m_ClampSampler = SDL_CreateGPUSampler(m_Device, &samplerInfo);

    if (m_BlurPipeline == nullptr || m_ClampSampler == nullptr)
    {
        LogMessage(LogLevel::Warning, "gpu", "Frosted glass is unavailable: %s", SDL_GetError());
        return false;
    }
    return true;
}

void Renderer::ReleaseBlurTextures()
{
    for (SDL_GPUTexture** texture : { &m_Blur.Down, &m_Blur.Across, &m_Blur.Result })
    {
        if (*texture != nullptr && m_Device != nullptr)
        {
            SDL_ReleaseGPUTexture(m_Device, *texture);
        }
        *texture = nullptr;
    }
    m_Blur = BlurTextures{};
}

bool Renderer::EnsureBlurTextures(int width, int height, int levels)
{
    if (m_Blur.Down != nullptr && m_Blur.Width == width && m_Blur.Height == height && m_Blur.Levels == levels)
    {
        return true;
    }
    ReleaseBlurTextures();

    auto Make = [&](int textureWidth, int textureHeight, int mipLevels) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = m_SwapchainFormat;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = static_cast<uint32_t>(textureWidth);
        info.height = static_cast<uint32_t>(textureHeight);
        info.layer_count_or_depth = 1;
        info.num_levels = static_cast<uint32_t>(mipLevels);
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        return SDL_CreateGPUTexture(m_Device, &info);
    };

    m_Blur.Width = width;
    m_Blur.Height = height;
    m_Blur.Levels = levels;
    m_Blur.ResultWidth = std::max(1, width >> (levels - 1));
    m_Blur.ResultHeight = std::max(1, height >> (levels - 1));
    m_Blur.Down = Make(width, height, levels);
    m_Blur.Across = Make(m_Blur.ResultWidth, m_Blur.ResultHeight, 1);
    m_Blur.Result = Make(m_Blur.ResultWidth, m_Blur.ResultHeight, 1);
    if (m_Blur.Down == nullptr || m_Blur.Across == nullptr || m_Blur.Result == nullptr)
    {
        LogMessage(LogLevel::Error, "gpu", "Could not make the textures frosted glass blurs into: %s",
                   SDL_GetError());
        ReleaseBlurTextures();
        return false;
    }
    return true;
}

SDL_GPUTexture* Renderer::BlurBackdrop(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target, int width,
                                       int height, float radius)
{
    // The blur's deviation is half its radius, as for shadows. The copy is
    // shrunk by halves until the deviation is at most six texels there, which
    // keeps the kernel short however large the radius.
    const float sigma = std::max(radius * 0.5f, 0.5f);
    int levels = 1;
    while (levels < 6 && sigma / static_cast<float>(1 << levels) > 6.0f)
    {
        ++levels;
    }

    const int halfWidth = std::max(1, width / 2);
    const int halfHeight = std::max(1, height / 2);
    if (!EnsureBlurTextures(halfWidth, halfHeight, levels))
    {
        return nullptr;
    }

    // Shrunk a half at a time, each step averaging the one before, so nothing
    // is skipped and a moving backdrop does not shimmer through the glass.
    SDL_GPUBlitInfo blit{};
    blit.source.texture = target;
    blit.source.w = static_cast<uint32_t>(width);
    blit.source.h = static_cast<uint32_t>(height);
    blit.destination.texture = m_Blur.Down;
    blit.destination.w = static_cast<uint32_t>(halfWidth);
    blit.destination.h = static_cast<uint32_t>(halfHeight);
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(commandBuffer, &blit);

    for (int level = 1; level < levels; ++level)
    {
        SDL_GPUBlitInfo shrink{};
        shrink.source.texture = m_Blur.Down;
        shrink.source.mip_level = static_cast<uint32_t>(level - 1);
        shrink.source.w = static_cast<uint32_t>(std::max(1, halfWidth >> (level - 1)));
        shrink.source.h = static_cast<uint32_t>(std::max(1, halfHeight >> (level - 1)));
        shrink.destination.texture = m_Blur.Down;
        shrink.destination.mip_level = static_cast<uint32_t>(level);
        shrink.destination.w = static_cast<uint32_t>(std::max(1, halfWidth >> level));
        shrink.destination.h = static_cast<uint32_t>(std::max(1, halfHeight >> level));
        shrink.load_op = SDL_GPU_LOADOP_DONT_CARE;
        shrink.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(commandBuffer, &shrink);
    }

    struct BlurUniforms
    {
        float Step[2];
        float Sigma;
        float Taps;
        float Level;
        float Padding[3];
    };
    const float lowSigma = std::max(sigma / static_cast<float>(1 << levels), 0.5f);
    const float taps = std::min(std::ceil(lowSigma * 3.0f), 24.0f);

    auto Pass = [&](SDL_GPUTexture* source, float sourceLevel, SDL_GPUTexture* destination, float stepX,
                    float stepY) {
        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = destination;
        colorTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
        if (pass == nullptr)
        {
            return false;
        }
        SDL_BindGPUGraphicsPipeline(pass, m_BlurPipeline);
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = source;
        binding.sampler = m_ClampSampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        const BlurUniforms uniforms{ { stepX, stepY }, lowSigma, taps, sourceLevel, { 0.0f, 0.0f, 0.0f } };
        SDL_PushGPUFragmentUniformData(commandBuffer, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        return true;
    };

    const float texelX = 1.0f / static_cast<float>(m_Blur.ResultWidth);
    const float texelY = 1.0f / static_cast<float>(m_Blur.ResultHeight);
    if (!Pass(m_Blur.Down, static_cast<float>(levels - 1), m_Blur.Across, texelX, 0.0f) ||
        !Pass(m_Blur.Across, 0.0f, m_Blur.Result, 0.0f, texelY))
    {
        return nullptr;
    }
    return m_Blur.Result;
}

} // namespace opane
