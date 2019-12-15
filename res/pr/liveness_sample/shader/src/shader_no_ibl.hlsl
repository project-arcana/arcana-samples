struct vs_in
{
    float3 P : POSITION;
    float3 N : NORMAL;
    float2 Texcoord : TEXCOORD;
    float4 Tangent : TANGENT;
};

struct vs_out
{
    float4 SV_P : SV_POSITION;
    float3x3 TBN : VO_TBN;
    float3 WorldPos : VO_WP;
    float2 Texcoord : VO_UV;
};

struct camera_constants
{
    float4x4 view_proj;
    float3 cam_pos;
    float runtime;
};

struct model_constants
{
    float4x4 model;
};

static const float PI = 3.141592;
static const float Epsilon = 0.00001;
// Constant normal incidence Fresnel factor for all dielectrics.
static const float3 Fdielectric = 0.04;

Texture2D g_albedo                                  : register(t0, space1);
Texture2D g_normal                                  : register(t1, space1);
Texture2D g_metallic                                : register(t2, space1);
Texture2D g_roughness                               : register(t3, space1);

SamplerState g_sampler                              : register(s0, space1);

ConstantBuffer<camera_constants> g_frame_data       : register(b0, space0);
ConstantBuffer<model_constants> g_model_data        : register(b0, space1);


vs_out main_vs(vs_in v_in)
{
    vs_out Out;

    const float4x4 mvp = mul(g_frame_data.view_proj, g_model_data.model);
    Out.SV_P = mul(mvp, float4(v_in.P, 1.0));
    Out.WorldPos = mul(g_model_data.model, float4(v_in.P, 1.0)).xyz;
    float3 N = normalize(mul((float3x3)g_model_data.model, v_in.N));
    Out.Texcoord = v_in.Texcoord;
    
    float3 tangent = mul((float3x3)g_model_data.model, v_in.Tangent.xyz);
    // gram-schmidt re-orthogonalization
    tangent = normalize(tangent) - dot(normalize(tangent), v_in.N) * v_in.N;

    float3 bitangent = mul((float3x3)g_model_data.model, cross(tangent, v_in.N));
    Out.TBN = transpose(float3x3(normalize(tangent), normalize(bitangent), normalize(N))); 

    return Out;
}

float get_pointlight_contrib(float3 normal, float3 worldpos, float3 lightpos)
{
    const float3 L = normalize(lightpos - worldpos);
    return saturate(dot(normal, L));
}

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float ndfGGX(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

// Shlick's approximation of the Fresnel factor.
float3 fresnelSchlick(float3 F0, float cosTheta)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 calculateDirectLight(float3 Li, float3 Lo, float cosLo, float3 Lradiance, float3 N, float3 F0, float roughness, float metalness, float3 albedo)
{
    // Half-vector between Li and Lo.
    float3 Lh = normalize(Li + Lo);

    // Calculate angles between surface normal and various light vectors.
    float cosLi = max(0.0, dot(N, Li));
    float cosLh = max(0.0, dot(N, Lh));

    // Calculate Fresnel term for direct lighting. 
    float3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
    // Calculate normal distribution for specular BRDF.
    float D = ndfGGX(cosLh, roughness);
    // Calculate geometric attenuation for specular BRDF.
    float G = gaSchlickGGX(cosLi, cosLo, roughness);

    // Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
    // Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
    // To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
    float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metalness);

    // Lambert diffuse BRDF.
    // We don't scale by 1/PI for lighting & material units to be more convenient.
    // See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
    float3 diffuseBRDF = kd * albedo;

    // Cook-Torrance specular microfacet BRDF.
    float3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

    // Total contribution for this light.
    return (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
}

float4 main_ps(vs_out p_in) : SV_TARGET
{
    float3 N = normalize(2.0 * g_normal.Sample(g_sampler, p_in.Texcoord).rgb - 1.0);
    N = normalize(mul(p_in.TBN, N));

    const float3 Lo = normalize(g_frame_data.cam_pos - p_in.WorldPos);
    
    const float3 albedo = g_albedo.Sample(g_sampler, p_in.Texcoord).rgb;
    const float metalness = g_metallic.Sample(g_sampler, p_in.Texcoord).r;
    const float roughness = g_roughness.Sample(g_sampler, p_in.Texcoord).r;

    // Angle between surface normal and outgoing light direction.
	float cosLo = max(0.0, dot(N, Lo));

	// Fresnel reflectance at normal incidence (for metals use albedo color).
	float3 F0 = lerp(Fdielectric, albedo, metalness);

    float3 directLighting = 0.0;

    float3 Li =  normalize(float3(-2, 2, 3) - p_in.WorldPos);
    directLighting += calculateDirectLight(Li, Lo, cosLo, 1.0, N, F0, roughness, metalness, albedo);
    return float4(directLighting, 1.0);
} 
