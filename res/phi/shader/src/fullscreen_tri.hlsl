

void main(
    uint vid : SV_VertexID,
    out noperspective float2 Texcoord : TEXCOORD0,
    out float4 SV_P : SV_Position
)
{
    Texcoord = float2((vid << 1) & 2, vid & 2);
    SV_P = float4(Texcoord * 2.0f + -1.0f, 0.0f, 1.0f);
    SV_P.y = -SV_P.y;
}
