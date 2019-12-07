#pragma once

#include <iostream>

#include <clean-core/capped_vector.hh>
#include <clean-core/defer.hh>

#include <typed-geometry/tg.hh>

#include <task-dispatcher/td.hh>

#include <phantasm-renderer/backend/Backend.hh>
#include <phantasm-renderer/backend/assets/image_loader.hh>
#include <phantasm-renderer/backend/assets/mesh_loader.hh>
#include <phantasm-renderer/backend/assets/vertex_attrib_info.hh>
#include <phantasm-renderer/backend/command_stream.hh>
#include <phantasm-renderer/backend/device_tentative/timer.hh>
#include <phantasm-renderer/backend/device_tentative/window.hh>
#include <phantasm-renderer/default_config.hh>

namespace pr_test
{
inline auto const get_projection_matrix = [](int w, int h) -> tg::mat4 { return tg::perspective_directx(60_deg, w / float(h), 0.1f, 100.f); };

inline auto const get_view_matrix = [](double runtime) -> tg::mat4 {
    constexpr auto target = tg::pos3(0, 1.45f, 0);
    const auto cam_pos = tg::rotate_y(tg::pos3(1, 1.5f, 1) * 10.f, tg::radians(float(runtime * 0.05)))
                         + tg::vec3(0, tg::sin(tg::radians(float(runtime * 0.125))) * 10.f, 0);
    return tg::look_at_directx(cam_pos, target, tg::vec3(0, 1, 0));
};

inline auto const get_view_projection_matrix
    = [](double runtime, int w, int h) -> tg::mat4 { return get_projection_matrix(w, h) * get_view_matrix(runtime); };

inline auto const get_model_matrix = [](tg::vec3 pos, double runtime, unsigned index) -> tg::mat4 {
    constexpr auto model_scale = 1.25f;
    return tg::translation(pos + tg::vec3(index % 9, index % 6, index % 9))
           * tg::rotation_y(tg::radians((float(runtime * 2.) + float(index) * 0.5f * tg::pi_scalar<float>)*(index % 2 == 0 ? -1 : 1)))
           * tg::scaling(model_scale, model_scale, model_scale);
};

inline constexpr auto sample_mesh_path = "res/pr/liveness_sample/mesh/apollo.obj";
inline constexpr auto sample_texture_path = "res/pr/liveness_sample/texture/uv_checker.png";
inline constexpr auto num_render_threads = 8;

inline auto const get_backend_config = [] {
    pr::backend::backend_config config;
    config.present_mode = pr::backend::present_mode::allow_tearing;
    config.adapter_preference = pr::backend::adapter_preference::highest_vram;
    config.num_threads = td::system::num_logical_cores();

    config.validation =
#ifdef NDEBUG
        pr::backend::validation_level::off;
#else
        pr::backend::validation_level::on_extended_dred;
#endif

    return config;
};

struct model_matrix_data
{
    static constexpr auto num_instances = 16;

    struct padded_instance
    {
        tg::mat4 model_mat;
        char padding[256 - sizeof(tg::mat4)];
    };

    static_assert(sizeof(padded_instance) == 256);
    cc::array<padded_instance, num_instances> model_matrices;

    void fill(double runtime)
    {
        cc::array constexpr model_positions
            = {tg::vec3(1, 0, 0), tg::vec3(0, 1, 0), tg::vec3(0, 0, 1), tg::vec3(-1, 0, 0), tg::vec3(0, -1, 0), tg::vec3(0, 0, -1)};

        auto mp_i = 0u;
        for (auto i = 0u; i < num_instances; ++i)
        {
            model_matrices[i].model_mat = get_model_matrix(model_positions[mp_i] * 0.25f * float(i), runtime / (double(i + 1) * .15), i);


            ++mp_i;
            if (mp_i == model_positions.size())
                mp_i -= model_positions.size();
        }
    }
};

// Sample code
struct sample_config
{
    char const* window_title;
    char const* path_render_vs;
    char const* path_render_ps;
    char const* path_blit_vs;
    char const* path_blit_ps;

    /// whether to align MIP rows by 256 bytes (D3D12: yes, Vulkan: no)
    bool align_mip_rows;
};

void run_sample(pr::backend::Backend& backend, pr::backend::backend_config const& config, sample_config const& sample_config);

// MIP map upload utilities

void copy_mipmaps_to_texture(pr::backend::command_stream_writer& writer,
                             pr::backend::handle::resource upload_buffer,
                             std::byte* upload_buffer_map,
                             pr::backend::handle::resource dest_texture,
                             pr::backend::format format,
                             pr::backend::assets::image_size const& img_size,
                             pr::backend::assets::image_data const& img_data,
                             bool use_d3d12_per_row_alingment);

unsigned get_mipmap_upload_size(pr::backend::format format, pr::backend::assets::image_size const& img_size);

}
