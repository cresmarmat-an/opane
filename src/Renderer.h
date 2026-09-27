// The UI renderer: one pipeline, one dynamic vertex and index buffer, and a
// texture pool. Not installed and not part of the public API.

#pragma once

#include "Internal.h"
#include "Materials.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace opane
{

class Renderer
{
public:
    bool Initialize(SDL_GPUDevice* device, SDL_Window* window);
    void Shutdown();

    TextureId CreateTexture(int width, int height, PixelFormat format, const void* pixels);

    // One pointer per mip level, largest first, each level half the size of
    // the one before and never smaller than 1x1.
    TextureId CreateTextureLevels(int width, int height, PixelFormat format,
                                  const std::vector<const void*>& levels);

    // Replaces a texture's pixels, and its size when that has changed. The
    // handle stays the same, so anything already drawn this frame naming this
    // texture still names the right one. This is how the glyph atlas grows.
    // One pointer per mip level, largest first.
    bool UpdateTexture(TextureId id, int width, int height, PixelFormat format,
                       const std::vector<const void*>& levels);

    // Registers a texture this renderer does not own, so it is never released
    // here.
    TextureId AdoptTexture(SDL_GPUTexture* texture, int width, int height);

    void DestroyTexture(TextureId id);
    Vec2 GetTextureSize(TextureId id) const;
    TextureId GetWhiteTexture() const { return m_WhiteTexture; }

    // Copies this frame's geometry to the GPU. Must run before the render pass
    // begins, on the same command buffer.
    void Upload(SDL_GPUCommandBuffer* commandBuffer, const DrawList& drawList);

    // Issues one draw per batch, switching pipelines where a batch carries a
    // material. origin is the window position of the target's top-left corner:
    // zero for the window, an element's position for its own render target.
    //
    // A backdrop batch (frosted glass) ends the pass, blurs a copy of
    // target, and begins the pass again, which is why the pass is passed by
    // reference. target must be samplable for that; without one, the glass is
    // drawn as its tint alone.
    void Render(SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass*& renderPass, SDL_GPUTexture* target,
                const DrawList& drawList, int viewportWidth, int viewportHeight,
                detail::MaterialStore* materials, Vec2 origin = Vec2{});

    // Whether the list has frosted glass, and so must be rendered into a
    // texture that can be read back, rather than straight to the window.
    bool NeedsSampledTarget(const DrawList& drawList) const;

    // A strip holding a gradient, premultiplied, for gradients a box's corner
    // colours cannot describe on their own. Made once for each set of stops
    // and kept.
    TextureId GetGradientTexture(const std::vector<GradientStop>& stops);

    SDL_GPUTextureFormat GetTargetFormat() const { return m_SwapchainFormat; }

    // A texture that can be both drawn into and sampled, in the swapchain's
    // format so the interface pipelines can target it.
    TextureId CreateRenderTarget(int width, int height);

    SDL_GPUTexture* GetTexture(TextureId id) const { return ResolveTexture(id); }

    // Grows the geometry buffers once, before a frame uploads several draw
    // lists, so none of them has to reallocate mid-frame.
    void Reserve(size_t vertexCount, size_t indexCount) { EnsureBufferCapacity(vertexCount, indexCount); }

    // Builds a pipeline pairing opane's vertex stage with a user fragment
    // shader, in the format this device takes. The vertex stage is shared so
    // a material cannot change how an element is positioned or batched.
    SDL_GPUGraphicsPipeline* CreatePipelineForFragmentShader(const ShaderBytecode& bytecode,
                                                             const char* entryPoint) const;
    void ReleasePipeline(SDL_GPUGraphicsPipeline* pipeline) const;

    uint32_t GetLastDrawCallCount() const { return m_LastDrawCalls; }
    SDL_GPUDevice* GetDevice() const { return m_Device; }

private:
    struct TextureRecord
    {
        SDL_GPUTexture* Texture = nullptr;
        uint32_t Generation = 0;
        bool Alive = false;
        bool Owned = true;
        int Width = 0;
        int Height = 0;
        uint32_t Levels = 1;
    };

    bool EnsureBufferCapacity(size_t vertexCount, size_t indexCount);
    SDL_GPUTexture* ResolveTexture(const TextureId& id) const;
    SDL_GPUGraphicsPipeline* BuildPipeline(SDL_GPUShader* fragmentShader) const;

    // The blur behind frosted glass: target, shrunk and blurred by radius
    // pixels, in a texture of its own. Null when it cannot be made.
    bool CreateBlurPipeline();
    SDL_GPUTexture* BlurBackdrop(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target, int width,
                                 int height, float radius);

    struct BlurTextures
    {
        SDL_GPUTexture* Down = nullptr;   // half size, with a mip chain the copy is shrunk down
        SDL_GPUTexture* Across = nullptr; // the first pass's result
        SDL_GPUTexture* Result = nullptr; // the second's, which the glass samples
        int Width = 0;                    // of Down's first level
        int Height = 0;
        int Levels = 0;
        int ResultWidth = 0;
        int ResultHeight = 0;
    };
    void ReleaseBlurTextures();
    bool EnsureBlurTextures(int width, int height, int levels);

    SDL_GPUDevice* m_Device = nullptr;

    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr;
    SDL_GPUSampler* m_Sampler = nullptr;

    // Kept alive so materials can pair their fragment stage with it.
    SDL_GPUShader* m_VertexShader = nullptr;
    SDL_GPUTextureFormat m_SwapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_GPUBuffer* m_VertexBuffer = nullptr;
    SDL_GPUBuffer* m_IndexBuffer = nullptr;
    SDL_GPUTransferBuffer* m_TransferBuffer = nullptr;

    size_t m_VertexCapacity = 0;
    size_t m_IndexCapacity = 0;

    std::vector<TextureRecord> m_Textures;
    std::vector<uint32_t> m_FreeTextures;
    TextureId m_WhiteTexture;

    uint32_t m_UploadedIndexCount = 0;
    uint32_t m_LastDrawCalls = 0;

    SDL_GPUGraphicsPipeline* m_BlurPipeline = nullptr;
    SDL_GPUSampler* m_ClampSampler = nullptr;
    BlurTextures m_Blur;
    bool m_BlurUnavailableReported = false;

    // Gradient strips by their stops, with the upload they were last used
    // before, so a full cache recycles one nothing drawn this frame needs.
    struct GradientRecord
    {
        TextureId Texture;
        uint64_t LastUsed = 0;
    };
    std::unordered_map<std::string, GradientRecord> m_Gradients;
    uint64_t m_UploadCount = 0;
};

} // namespace opane
