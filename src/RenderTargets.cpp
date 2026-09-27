#include "RenderTargets.h"

#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace opane::detail
{

void RenderTargetStore::Initialize(Renderer* renderer, FontStore* fonts)
{
    m_Renderer = renderer;
    m_Fonts = fonts;
}

void RenderTargetStore::Shutdown()
{
    for (auto& record : m_Records)
    {
        ReleaseRecord(*record);
    }
    m_Records.clear();
    m_Order.clear();
    m_Renderer = nullptr;
}

void RenderTargetStore::ReleaseRecord(RenderTargetRecord& record)
{
    if (m_Renderer != nullptr)
    {
        if (record.Ping.IsValid())
        {
            m_Renderer->DestroyTexture(record.Ping);
        }
        if (record.Pong.IsValid())
        {
            m_Renderer->DestroyTexture(record.Pong);
        }
    }
    record.Ping = TextureId{};
    record.Pong = TextureId{};
}

void RenderTargetStore::BeginFrame()
{
    for (auto& record : m_Records)
    {
        record->UsedThisFrame = false;
    }
    m_Order.clear();
}

void RenderTargetStore::EndFrame()
{
    // Released a frame after last use rather than immediately, so an element
    // that is hidden for one frame does not pay to rebuild its textures.
    auto unused = std::remove_if(m_Records.begin(), m_Records.end(),
                                 [&](const std::unique_ptr<RenderTargetRecord>& record) {
                                     if (record->UsedThisFrame)
                                     {
                                         return false;
                                     }
                                     ReleaseRecord(*record);
                                     return true;
                                 });
    m_Records.erase(unused, m_Records.end());
}

RenderTargetRecord* RenderTargetStore::Acquire(const Element* owner, const Rect& unitBounds,
                                               const std::vector<MaterialId>& postProcess, float scale)
{
    if (m_Renderer == nullptr)
    {
        return nullptr;
    }

    scale = scale > 0.0f ? scale : 1.0f;
    const Rect bounds{ unitBounds.X * scale, unitBounds.Y * scale, unitBounds.Width * scale,
                       unitBounds.Height * scale };

    // Whole pixels, rounded outward, so the texture always covers the element.
    const int width = static_cast<int>(std::ceil(bounds.Width));
    const int height = static_cast<int>(std::ceil(bounds.Height));

    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    RenderTargetRecord* record = nullptr;
    for (auto& candidate : m_Records)
    {
        if (candidate->Owner == owner)
        {
            record = candidate.get();
            break;
        }
    }

    if (record == nullptr)
    {
        m_Records.push_back(std::make_unique<RenderTargetRecord>());
        record = m_Records.back().get();
        record->Owner = owner;
    }

    if (record->Width != width || record->Height != height || !record->Ping.IsValid())
    {
        ReleaseRecord(*record);
        record->Ping = m_Renderer->CreateRenderTarget(width, height);
        record->Pong = m_Renderer->CreateRenderTarget(width, height);
        record->Width = width;
        record->Height = height;
    }

    record->Bounds = Rect{ std::floor(bounds.X), std::floor(bounds.Y), static_cast<float>(width),
                           static_cast<float>(height) };
    record->UnitBounds = Rect{ record->Bounds.X / scale, record->Bounds.Y / scale, record->Bounds.Width / scale,
                               record->Bounds.Height / scale };
    record->PostProcess = postProcess;
    record->UsedThisFrame = true;

    record->Contents.Clear();
    record->Contents.m_Viewport = record->Bounds;
    record->Contents.m_Fonts = m_Fonts;
    record->Contents.m_Renderer = m_Renderer;
    record->Contents.m_Scale = scale;

    m_Order.push_back(record);
    return record;
}

TextureId RenderTargetStore::FinalTexture(const RenderTargetRecord& record) const
{
    // The subtree renders into Ping, and each pass swaps, so an even number of
    // passes ends back in Ping.
    return (record.PostProcess.size() % 2 == 0) ? record.Ping : record.Pong;
}

bool RenderTargetStore::HasWork() const
{
    return !m_Order.empty();
}

void RenderTargetStore::GetLargestList(size_t& outVertices, size_t& outIndices) const
{
    for (const RenderTargetRecord* record : m_Order)
    {
        outVertices = std::max(outVertices, record->Contents.m_Vertices.size());
        outIndices = std::max(outIndices, record->Contents.m_Indices.size());
    }
}

void RenderTargetStore::RenderPass(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target,
                                   const DrawList& list, int width, int height, Vec2 origin,
                                   MaterialStore& materials)
{
    if (target == nullptr)
    {
        return;
    }

    // The upload is a copy pass, and a copy pass cannot run inside a render
    // pass, so it goes first. Buffers are cycled, so earlier passes in this
    // command buffer keep the geometry they were recorded with.
    m_Renderer->Upload(commandBuffer, list);

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = target;

    // Transparent, so whatever the subtree does not cover stays see-through
    // when the result is composited.
    colorTarget.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
    if (pass == nullptr)
    {
        return;
    }

    m_Renderer->Render(commandBuffer, pass, target, list, width, height, &materials, origin);
    if (pass != nullptr)
    {
        SDL_EndGPURenderPass(pass);
    }
}

void RenderTargetStore::RenderAll(SDL_GPUCommandBuffer* commandBuffer, MaterialStore& materials)
{
    if (m_Renderer == nullptr)
    {
        return;
    }

    // Innermost first: a target nested inside another was acquired later, and
    // the outer one's contents sample its finished texture.
    for (auto iterator = m_Order.rbegin(); iterator != m_Order.rend(); ++iterator)
    {
        RenderTargetRecord& record = **iterator;

        SDL_GPUTexture* ping = m_Renderer->GetTexture(record.Ping);
        SDL_GPUTexture* pong = m_Renderer->GetTexture(record.Pong);
        if (ping == nullptr || pong == nullptr)
        {
            continue;
        }

        const Vec2 origin{ record.Bounds.X, record.Bounds.Y };

        RenderPass(commandBuffer, ping, record.Contents, record.Width, record.Height, origin, materials);

        // Each pass reads the previous result through Surface and writes the
        // other texture.
        TextureId source = record.Ping;
        TextureId destination = record.Pong;

        for (const MaterialId& material : record.PostProcess)
        {
            const Rect full{ 0.0f, 0.0f, static_cast<float>(record.Width),
                             static_cast<float>(record.Height) };

            m_PassList.Clear();
            m_PassList.m_Viewport = full;
            m_PassList.m_Fonts = m_Fonts;
            m_PassList.m_Renderer = m_Renderer;

            // A pass works in the texture's own pixels, not in interface units.
            m_PassList.m_Scale = 1.0f;
            m_PassList.SetMaterial(material);
            m_PassList.DrawPremultipliedTexture(full, source);

            RenderPass(commandBuffer, m_Renderer->GetTexture(destination), m_PassList, record.Width,
                       record.Height, Vec2{}, materials);

            std::swap(source, destination);
        }
    }
}

} // namespace opane::detail
