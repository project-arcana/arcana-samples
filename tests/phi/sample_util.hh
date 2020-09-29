#pragma once

#include <typed-geometry/tg.hh>

#include <phantasm-hardware-interface/common/container/unique_buffer.hh>

#include "sse_vec.hh"

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
            SSEVec sum = SSEAdd(SSELoadAligned(&val[i * VecSize]),    //
                                SSELoadAligned(&rhs.val[i * VecSize]) //
            );

            SSEStoreAligned(sum, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator-=(SHVec const& rhs)
    {
        for (auto i = 0; i < NumVecs; ++i)
        {
            SSEVec diff = SSESubtract(SSELoadAligned(&val[i * VecSize]),    //
                                      SSELoadAligned(&rhs.val[i * VecSize]) //
            );

            SSEStoreAligned(diff, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator*=(SHVec const& rhs)
    {
        for (auto i = 0; i < NumVecs; ++i)
        {
            SSEVec prod = SSEMultiply(SSELoadAligned(&val[i * VecSize]),    //
                                      SSELoadAligned(&rhs.val[i * VecSize]) //
            );

            SSEStoreAligned(prod, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator/=(float const& divisor)
    {
        float const rcp = 1.f / divisor;
        SSEVec const rcp_vec = SSEReplicateToVector(&rcp);

        for (auto i = 0; i < NumVecs; ++i)
        {
            SSEVec prod = SSEMultiply(SSELoadAligned(&val[i * VecSize]), //
                                      rcp_vec                            //
            );

            SSEStoreAligned(prod, &val[i * VecSize]);
        }

        return *this;
    }

    SHVec& operator*=(float const& scalar)
    {
        SSEVec const scalar_vec = SSEReplicateToVector(&scalar);

        for (auto i = 0; i < NumVecs; ++i)
        {
            SSEVec prod = SSEMultiply(SSELoadAligned(&val[i * VecSize]), //
                                      scalar_vec                         //
            );

            SSEStoreAligned(prod, &val[i * VecSize]);
        }

        return *this;
    }

    friend SHVec operator*(SHVec const& lhs, float const& scalar)
    {
        SSEVec const scalar_vec = SSEReplicateToVector(&scalar);

        SHVec res;
        for (auto i = 0; i < NumVecs; ++i)
        {
            SSEVec prod = SSEMultiply(SSELoadAligned(&lhs.val[i * VecSize]), //
                                      scalar_vec                             //
            );

            SSEStoreAligned(prod, &res.val[i * VecSize]);
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
