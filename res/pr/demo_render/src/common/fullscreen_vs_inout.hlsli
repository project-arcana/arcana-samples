
struct vs_in
{
    uint vid            : SV_VertexID;
};

struct vs_out
{
    float4 SV_P         : SV_POSITION;
    float2 Texcoord     : TEXCOORD;
};
