#include <nexus/test.hh>

#include <arcana-incubator/device-abstraction/device_abstraction.hh>

#include <phantasm-renderer/Context.hh>
#include <phantasm-renderer/Frame.hh>
#include <phantasm-renderer/PrimitivePipeline.hh>
#include <phantasm-renderer/immediate.hh>

namespace
{
struct my_vertex
{
    tg::pos3 pos;
    tg::color3 color;
};
template <class I>
constexpr void introspect(I&& i, my_vertex& v)
{
    i(v.pos, "pos");
    i(v.color, "color");
}

struct camera_data : pr::resources
{
    tg::mat4 proj;
    tg::mat4 view;
};
template <class I>
constexpr void introspect(I&& i, camera_data& v)
{
    i(v.proj, "proj");
    i(v.view, "view");
}

struct instance_data : pr::immediate
{
    tg::mat4 model;
};
template <class I>
constexpr void introspect(I&& i, instance_data& v)
{
    i(v.model, "model");
}


}

TEST("pr::api")
{
    //

    inc::da::SDLWindow window;
    window.initialize("api test");

    pr::Context ctx(phi::window_handle{window.getSdlWindow()});

    ctx.make_shader(R"(

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
                        uint model_mat_index;
                    };

                    static const float PI = 3.141592;
                    static const float Epsilon = 0.00001;
                    // Constant normal incidence Fresnel factor for all dielectrics.
                    static const float3 Fdielectric = 0.04;

                    StructuredBuffer<float4x4> g_model_matrices         : register(t0, space0);

                    Texture2D g_albedo                                  : register(t0, space1);
                    Texture2D g_normal                                  : register(t1, space1);
                    Texture2D g_metallic                                : register(t2, space1);
                    Texture2D g_roughness                               : register(t3, space1);

                    SamplerState g_sampler                              : register(s0, space1);

                    TextureCube g_ibl_specular                          : register(t0, space2);
                    TextureCube g_ibl_irradiance                        : register(t1, space2);
                    Texture2D g_ibl_specular_lut                        : register(t2, space2);

                    SamplerState g_lut_sampler                          : register(s0, space2);

                    ConstantBuffer<camera_constants> g_frame_data       : register(b0, space0);

                    [[vk::push_constant]] ConstantBuffer<model_constants> g_model_data        : register(b1, space0);

                    vs_out main_vs(vs_in v_in)
                    {
                        vs_out Out;

                        const float4x4 model = g_model_matrices[g_model_data.model_mat_index];
                        const float4x4 mvp = mul(g_frame_data.view_proj, model);
                        Out.SV_P = mul(mvp, float4(v_in.P, 1.0));
                        Out.WorldPos = mul(model, float4(v_in.P, 1.0)).xyz;
                        float3 N = normalize(mul((float3x3)model, v_in.N));
                        Out.Texcoord = v_in.Texcoord;

                        float3 tangent = mul((float3x3)model, v_in.Tangent.xyz);
                        // gram-schmidt re-orthogonalization
                        tangent = normalize(tangent) - dot(normalize(tangent), v_in.N) * v_in.N;

                        float3 bitangent = mul((float3x3)model, cross(tangent, v_in.N));
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
                        float cosLi = max(0.0, do(N, Li));
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

                    // Returns number of mipmap levels for specular IBL environment map.
                    uint querySpecularTextureLevels()
                    {
                        uint width, height, levels;
                        g_ibl_specular.GetDimensions(0, width, height, levels);
                        return levels;
                    }

                    float3 calculateIndirectLight(float3 N, float3 F0, float3 Lo, float cosLo, float metalness, float roughness, float3 albedo)
                    {
                        // Sample diffuse irradiance at normal direction.
                        float3 irradiance = g_ibl_irradiance.Sample(g_sampler, N).rgb;

                        // Calculate Fresnel term for ambient lighting.
                        // Since we use pre-filtered cubemap(s) and irradiance is coming from many directions
                        // use cosLo instead of angle with light's half-vector (cosLh above).
                        // See: https://seblagarde.wordpress.com/2011/08/17/hello-world/
                        float3 F = fresnelSchlick(F0, cosLo);

                        // Specular reflection vector.
                        float3 Lr = 2.0 * cosLo * N - Lo;

                        // Get diffuse contribution factor (as with direct lighting).
                        float3 kd = lerp(1.0 - F, 0.0, metalness);

                        // Irradiance map contains exitant radiance assuming Lambertian BRDF, no need to scale by 1/PI here either.
                        float3 diffuseIBL = kd * albedo * irradiance;

                        // Sample pre-filtered specular reflection environment at correct mipmap level.
                        uint specularTextureLevels = querySpecularTextureLevels();
                        float3 specularIrradiance = g_ibl_specular.SampleLevel(g_sampler, Lr, roughness * specularTextureLevels).rgb;

                        // Split-sum approximation factors for Cook-Torrance specular BRDF.
                        float2 specularBRDF = g_ibl_specular_lut.Sample(g_lut_sampler, float2(cosLo, roughness)).rg;

                        // Total specular IBL contribution.
                        float3 specularIBL = (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;

                        // Total ambient lighting contribution.
                        return diffuseIBL + specularIBL;
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
                        float3 indirect = calculateIndirectLight(N, F0, Lo, cosLo, metalness, roughness, albedo);
                        return float4(directLighting + indirect, 1.0);
                    }


                    )", "main_ps", phi::shader_stage::pixel);

    int w = 1;
    int h = 1;

    cc::vector<my_vertex> vertices; // = ...
    vertices.emplace_back();

    camera_data data;
    data.proj = tg::mat4(); // ...
    data.view = tg::mat4(); // ...

    instance_data instanceA; // = ...
    instance_data instanceB; // = ...

    auto t_depth = ctx.make_target({w, h}, pr::format::depth32f);
    auto t_color = ctx.make_target({w, h}, pr::format::rgba16f);
    auto vertex_buffer = ctx.make_upload_buffer(sizeof(my_vertex) * vertices.size(), sizeof(my_vertex));

    {
//        auto frame = ctx.make_frame();

//        auto fshader = ctx.make_fragment_shader<pr::format::rgba16f>("<CODE>");
//        auto vshader = ctx.make_vertex_shader<my_vertex>("<CODE>");

//        {
//            // trying to start another pass while this one is active is a CONTRACT VIOLATION
//            // by default sets viewport to min over image sizes
//            auto fb = frame.render_to(t_color, t_depth).bind(data);

//            // framebuffer + shader + config = pass
//            auto pass = fb.pipeline<instance_data>(vshader, fshader, pr::default_config);

//            // issue draw command (explicit bind)
//            pass.bind(instanceA).draw(vertex_buffer);

//            // issue draw command (variadic draw)
//            pass.draw(instanceB, vertex_buffer);
//        }

        // submit frame
//        ctx.submit(frame);
    }
}
