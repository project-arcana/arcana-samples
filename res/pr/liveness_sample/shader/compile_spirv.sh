docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader.hlsl -T ps_6_0 -E mainPS -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./spirv/pixel.spv
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader.hlsl -T vs_6_0 -E mainVS -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./spirv/vertex.spv

docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader_blit_fs.hlsl -T ps_6_0 -E mainPS -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./spirv/pixel_blit.spv
docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./shader_blit_fs.hlsl -T vs_6_0 -E mainVS -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./spirv/vertex_blit.spv
