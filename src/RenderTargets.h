// Render-target elements and post-process chains.
// Not installed and not part of the public API.
//
// An element with RenderToTexture or PostProcess set paints its subtree into a
// draw list of its own. Before the frame's main pass, that list is rendered
// into a texture the element's size, then each post-process material runs over
// it, ping-ponging between two textures. The main draw list composites the
// final one where the element sits.

#pragma once

#include "Internal.h"
#include "Materials.h"

#include <memory>
#include <vector>

namespace opane
{

class Renderer;

namespace detail
{

struct RenderTargetRecord
{
    const Element* Owner = nullptr;

    DrawList Contents;

    // Where the texture sits, in pixels, and the same place in interface
    // units, which is what the quad showing it is drawn at.
    Rect Bounds;
    Rect UnitBounds;
    int Width = 0;
    int Height = 0;

    // Two textures, because a pass cannot read the texture it writes.
    TextureId Ping;
    TextureId Pong;

    std::vector<MaterialId> PostProcess;
    bool UsedThisFrame = false;
};

class RenderTargetStore
{
public:
    void Initialize(Renderer* renderer, FontStore* fonts);
    void Shutdown();

    // Marks every record unused. Anything still unused at EndFrame belonged to
    // an element that was removed or turned the feature off.
    void BeginFrame();
    void EndFrame();

    // Returns the record for this element, creating or resizing its textures
    // as needed, with Contents cleared and ready to paint into.
    // Bounds are in interface units; the texture is made at the pixel size
    // they cover, so a render-target element stays sharp at any scale.
    RenderTargetRecord* Acquire(const Element* owner, const Rect& bounds,
                                const std::vector<MaterialId>& postProcess, float scale);

    // Which of the two textures holds the finished image, decided by how many
    // post-process passes run. Known at paint time, so the main draw list can
    // reference it before anything has rendered.
    TextureId FinalTexture(const RenderTargetRecord& record) const;

    // Renders every record used this frame. Must run before the main pass
    // opens, on the same command buffer. Nested targets render innermost
    // first, so an outer target sees its children already finished.
    void RenderAll(SDL_GPUCommandBuffer* commandBuffer, MaterialStore& materials);

    // The largest draw list about to be uploaded, so the renderer can size its
    // buffers once rather than mid-frame.
    void GetLargestList(size_t& outVertices, size_t& outIndices) const;

    bool HasWork() const;

private:
    void ReleaseRecord(RenderTargetRecord& record);
    void RenderPass(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target,
                    const DrawList& list, int width, int height, Vec2 origin,
                    MaterialStore& materials);

    Renderer* m_Renderer = nullptr;
    FontStore* m_Fonts = nullptr;

    // Records are held by pointer so a DrawList never moves while something
    // refers to it.
    std::vector<std::unique_ptr<RenderTargetRecord>> m_Records;

    // Acquisition order this frame. Outer targets are acquired before the
    // targets nested inside them, so rendering walks this backwards.
    std::vector<RenderTargetRecord*> m_Order;

    DrawList m_PassList;
};

} // namespace detail
} // namespace opane
