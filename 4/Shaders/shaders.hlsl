cbuffer cbPerObject : register(b0)
{
    float4x4 WorldViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
};

struct PSInput
{
    float4 PositionH : SV_POSITION;
    float4 Color : COLOR;
};

PSInput VS(VSInput vin)
{
    PSInput vout;
    vout.PositionH = mul(float4(vin.Position, 1.0f), WorldViewProj);
    vout.Color = vin.Color;
    return vout;
}

float4 PS(PSInput pin) : SV_TARGET
{
    return pin.Color;
}