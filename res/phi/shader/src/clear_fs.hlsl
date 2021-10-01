
float4 main_ps(float2 uv : TEXCOORD0) : SV_TARGET
{
    return float4(uv.x, uv.y, 1.f, 1.f);
}
