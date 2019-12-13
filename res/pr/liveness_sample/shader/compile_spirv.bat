:: This looks much worse than it is
:: Arguments to any shader compiliation are:
:: -T                           The target, of the form ps/vs/... followed by the shading model, 6_0
:: -E                           The entry point, symbol literal
:: -spirv                       Flag to enable spirv output
:: -fspv-target-env             Flag to set the target vulkan API version, 1.1 in our case
:: -fvk-invert-y                Flag to flip SV_Position.y at the end of vs/ds/gs stages to accomodate vulkans coordinate system
:: -Fo                          The output file path
:: -fvk-[X]-shift [Y] [Z]       Flags to shift HLSL registers of type X in space Z upwards by Y
::
::                              We map registers like this:
::                              register(b[n], space[s])        -> layout(set = [s], binding = [n] + 0)
::                              register(t[n], space[s])        -> layout(set = [s], binding = [n] + 1000)
::                              register(u[n], space[s])        -> layout(set = [s], binding = [n] + 2000)
::                              register(s[n], space[s])        -> layout(set = [s], binding = [n] + 3000)
::
::                              Registers have to get shifted upwards because otherwise dxc just overlaps them
::                              The shifting scheme is directly coupled with our way of using shader arguments
::                              
::                              -fvk-[X]-shift [Y] all
::                              Same as above, but applies to all spaces at once

.\dxc_bin\dxc.exe .\shader.hlsl -T ps_6_0 -E main_ps -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\pixel.spv
.\dxc_bin\dxc.exe .\shader.hlsl -T vs_6_0 -E main_vs -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\vertex.spv

.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T ps_6_0 -E main_ps -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\blit_pixel.spv
.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T vs_6_0 -E main_vs -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\blit_vertex.spv

.\dxc_bin\dxc.exe .\mipgen.hlsl -T cs_6_0 -E main_cs -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\mipgen.spv
.\dxc_bin\dxc.exe .\mipgen_gamma.hlsl -T cs_6_0 -E main_cs -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\mipgen_gamma.spv
.\dxc_bin\dxc.exe .\mipgen_array.hlsl -T cs_6_0 -E main_cs -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\mipgen_array.spv

pause