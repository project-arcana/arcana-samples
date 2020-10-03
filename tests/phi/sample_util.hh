#pragma once

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/common/container/unique_buffer.hh>
#include <phantasm-hardware-interface/common/sse_vec.hh>

namespace inc
{
class ImGuiPhantasmImpl;
namespace da
{
class SDLWindow;
}
}

namespace phi
{
class Backend;
}

namespace phi_test
{
phi::unique_buffer get_shader_binary(char const* name, char const* ending);

void initialize_imgui(inc::da::SDLWindow& window, phi::Backend& backend);
void shutdown_imgui();

constexpr size_t linear_index(tg::vec<4, size_t> dimensions, tg::vec<4, size_t> dimensional_i)
{
    return dimensional_i.x + dimensional_i.y * dimensions.x + dimensional_i.z * dimensions.x * dimensions.y
           + dimensional_i.w * dimensions.x * dimensions.y * dimensions.z;
}

constexpr tg::vec<4, size_t> dimensional_index(tg::vec<4, size_t> dimensions, size_t linear_i)
{
    auto const i1 = linear_i % dimensions.x;
    auto const i2 = ((linear_i - i1) / dimensions.x) % dimensions.y;
    auto const i3 = ((linear_i - i2 * dimensions.x - i1) / (dimensions.x * dimensions.y)) % dimensions.z;
    auto const i4
        = ((linear_i - i3 * dimensions.y * dimensions.x - i2 * dimensions.x - i1) / (dimensions.x * dimensions.y * dimensions.z)) % dimensions.w;
    return {i1, i2, i3, i4};
}

constexpr size_t linear_index(tg::span<size_t const> indices, tg::span<size_t const> dimensions)
{
    size_t res = 0;
    for (auto ind_i = 0u; ind_i < indices.size(); ++ind_i)
    {
        auto val = indices[ind_i];
        for (auto ind_dim = 0u; ind_dim < ind_i; ++ind_dim)
            val *= dimensions[ind_dim];

        res += val;
    }

    return res;
}

template <int SHOrder>
struct alignas(16) SHVec
{
    static_assert(SHOrder >= 1, "invalid spherical harmonics order");

    enum
    {
        Order = SHOrder,
        MaxBasis = Order * Order,
        VecSize = 4,
        NumVecs = (MaxBasis + VecSize - 1) / VecSize,
        NumTotalFloats = NumVecs * VecSize
    };

    float val[NumTotalFloats] = {};

    SHVec() = default;

    SHVec(float v0, float v1, float v2, float v3)
    {
        std::memset(val, 0, sizeof(val));

        val[0] = v0;
        val[1] = v1;
        val[2] = v2;
        val[3] = v3;
    }

public:
    float get_integral() const
    {
        // 2 * sqrt(pi)
        return val[0] * 3.5449077018110320545963349666823f;
    }

    void normalize()
    {
        auto const integral = get_integral();
        if (integral > .001f)
        {
            *this /= integral;
        }
    }

    tg::vec3 get_max_direction() const { return tg::normalize_safe(tg::vec3(-val[3], -val[1], val[2])); }

public:
    static SHVec BasisFunction(tg::vec3 vec);

    static SHVec AmbientFunction()
    {
        SHVec res;
        res.val[0] = 1.f / 3.5449077018110320545963349666823f;
        return res;
    }

public:
    SHVec& operator+=(SHVec const& rhs)
    {
        for (auto i = 0; i < NumVecs; ++i)
        {
            phi::util::SSEVec sum = phi::util::SSEAdd(phi::util::SSELoadAligned(&val[i * VecSize]),    //
                                                      phi::util::SSELoadAligned(&rhs.val[i * VecSize]) //
            );

            phi::util::SSEStoreAligned(sum, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator-=(SHVec const& rhs)
    {
        for (auto i = 0; i < NumVecs; ++i)
        {
            phi::util::SSEVec diff = phi::util::SSESubtract(phi::util::SSELoadAligned(&val[i * VecSize]),    //
                                                            phi::util::SSELoadAligned(&rhs.val[i * VecSize]) //
            );

            phi::util::SSEStoreAligned(diff, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator*=(SHVec const& rhs)
    {
        for (auto i = 0; i < NumVecs; ++i)
        {
            phi::util::SSEVec prod = phi::util::SSEMultiply(phi::util::SSELoadAligned(&val[i * VecSize]),    //
                                                            phi::util::SSELoadAligned(&rhs.val[i * VecSize]) //
            );

            phi::util::SSEStoreAligned(prod, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator/=(float const& divisor)
    {
        float const rcp = 1.f / divisor;
        phi::util::SSEVec const rcp_vec = phi::util::SSEReplicateToVector(&rcp);

        for (auto i = 0; i < NumVecs; ++i)
        {
            phi::util::SSEVec prod = phi::util::SSEMultiply(phi::util::SSELoadAligned(&val[i * VecSize]), //
                                                            rcp_vec                                       //
            );

            phi::util::SSEStoreAligned(prod, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator*=(float const& scalar)
    {
        phi::util::SSEVec const scalar_vec = phi::util::SSEReplicateToVector(&scalar);

        for (auto i = 0; i < NumVecs; ++i)
        {
            phi::util::SSEVec prod = phi::util::SSEMultiply(phi::util::SSELoadAligned(&val[i * VecSize]), //
                                                            scalar_vec                                    //
            );

            phi::util::SSEStoreAligned(prod, &val[i * VecSize]);
        }

        return *this;
    }

    friend SHVec operator*(SHVec const& lhs, float const& scalar)
    {
        phi::util::SSEVec const scalar_vec = phi::util::SSEReplicateToVector(&scalar);

        SHVec res;
        for (auto i = 0; i < NumVecs; ++i)
        {
            phi::util::SSEVec prod = phi::util::SSEMultiply(phi::util::SSELoadAligned(&lhs.val[i * VecSize]), //
                                                            scalar_vec                                        //
            );

            phi::util::SSEStoreAligned(prod, &res.val[i * VecSize]);
        }

        return res;
    }
};

template <>
inline SHVec<2> SHVec<2>::BasisFunction(tg::vec3 vec)
{
    SHVec<2> res;
    res.val[0] = 0.282095f;
    res.val[1] = -0.488603f * vec.y;
    res.val[2] = 0.488603f * vec.z;
    res.val[3] = -0.488603f * vec.x;
    return res;
}

template <>
inline SHVec<3> SHVec<3>::BasisFunction(tg::vec3 vec)
{
    SHVec<3> res;
    res.val[0] = 0.282095f;
    res.val[1] = -0.488603f * vec.y;
    res.val[2] = 0.488603f * vec.z;
    res.val[3] = -0.488603f * vec.x;

    res.val[4] = 1.092548f * vec.x * vec.y;
    res.val[5] = -1.092548f * vec.y * vec.z;
    res.val[6] = 0.315392f * (3.f * tg::pow2(vec.z) - 1.f);
    res.val[7] = -1.092548f * vec.x * vec.z;
    res.val[8] = 0.546274f * (tg::pow2(vec.x) - tg::pow2(vec.y));
    return res;
}

struct SHVecColor
{
    // one 3rd order SH per channel
    SHVec<3> R;
    SHVec<3> G;
    SHVec<3> B;

    void add_radiance(tg::color3 radiance, float weight, tg::vec3 direction)
    {
        auto const basis = SHVec<3>::BasisFunction(direction);

        R += basis * (radiance.r * weight);
        G += basis * (radiance.g * weight);
        B += basis * (radiance.b * weight);
    }

    void add_ambient(tg::color3 intensity)
    {
        auto const basis = SHVec<3>::AmbientFunction();

        R += basis * intensity.r;
        G += basis * intensity.g;
        B += basis * intensity.b;
    }
};

void preprocess_spherical_harmonics(SHVecColor const& sh_irradiance, tg::span<tg::vec4> out_data);

}
