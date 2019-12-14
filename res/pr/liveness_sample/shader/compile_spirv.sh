
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader.hlsl -T ps_6_0 -E main_ps -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/pixel.spv
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader.hlsl -T vs_6_0 -E main_vs -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/vertex.spv

docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader_blit_fs.hlsl -T ps_6_0 -E main_ps -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/blit_pixel.spv
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader_blit_fs.hlsl -T vs_6_0 -E main_vs -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/blit_vertex.spv

docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./mipgen.hlsl -T cs_6_0 -E main_cs -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/mipgen.spv
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./mipgen_gamma.hlsl -T cs_6_0 -E main_cs -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/mipgen_gamma.spv
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./mipgen_array.hlsl -T cs_6_0 -E main_cs -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/mipgen_array.spv
