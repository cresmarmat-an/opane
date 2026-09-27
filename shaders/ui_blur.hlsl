// opane's backdrop blur, for frosted glass.
//
// What is behind a glass box is copied, shrunk a few times, and blurred with a
// Gaussian in two passes (horizontal, then vertical), each drawing one
// triangle that covers the target. The box then shows the result inside its shape.

#ifdef __spirv__
#define OPANE_SAMPLED(slot, set) [[vk::combinedImageSampler]] [[vk::binding(slot, set)]]
#else
#define OPANE_SAMPLED(slot, set)
#endif

struct BlurInput
{
    float4 Position : SV_Position;
    float2 Uv       : TEXCOORD0;
};

BlurInput BlurVertexMain(uint vertexId : SV_VertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    BlurInput output;
    output.Position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.Uv = uv;
    return output;
}

OPANE_SAMPLED(0, 2) Texture2D Source : register(t0, space2);
OPANE_SAMPLED(0, 2) SamplerState SourceSampler : register(s0, space2);

cbuffer BlurUniforms : register(b0, space3)
{
    float2 Step;  // one texel along the blur's direction, in the source's UV units
    float Sigma;  // the Gaussian's deviation, in texels
    float Taps;   // how many texels either side are read
    float Level;  // the source's mip level
    float3 BlurPadding;
};

float4 BlurFragmentMain(BlurInput input) : SV_Target
{
    float4 total = Source.SampleLevel(SourceSampler, input.Uv, Level);
    float weights = 1.0;

    int taps = (int)Taps;
    [loop]
    for (int index = 1; index <= taps; ++index)
    {
        float weight = exp(-0.5 * float(index * index) / (Sigma * Sigma));
        total += (Source.SampleLevel(SourceSampler, input.Uv + Step * index, Level) +
                  Source.SampleLevel(SourceSampler, input.Uv - Step * index, Level)) *
                 weight;
        weights += 2.0 * weight;
    }
    return total / weights;
}
