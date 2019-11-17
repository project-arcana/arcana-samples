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

.\dxc_bin\dxc.exe .\shader.hlsl -T ps_6_0 -E mainPS -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 0 -fvk-u-shift 2000 0 -fvk-t-shift 1000 0 -fvk-s-shift 3000 0 -fvk-b-shift 0 1 -fvk-u-shift 2000 1 -fvk-t-shift 1000 1 -fvk-s-shift 3000 1 -fvk-b-shift 0 2 -fvk-u-shift 2000 2 -fvk-t-shift 1000 2 -fvk-s-shift 3000 2 -fvk-b-shift 0 3 -fvk-u-shift 2000 3 -fvk-t-shift 1000 3 -fvk-s-shift 3000 3 -Fo .\spirv\pixel.spv
.\dxc_bin\dxc.exe .\shader.hlsl -T vs_6_0 -E mainVS -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 0 -fvk-u-shift 2000 0 -fvk-t-shift 1000 0 -fvk-s-shift 3000 0 -fvk-b-shift 0 1 -fvk-u-shift 2000 1 -fvk-t-shift 1000 1 -fvk-s-shift 3000 1 -fvk-b-shift 0 2 -fvk-u-shift 2000 2 -fvk-t-shift 1000 2 -fvk-s-shift 3000 2 -fvk-b-shift 0 3 -fvk-u-shift 2000 3 -fvk-t-shift 1000 3 -fvk-s-shift 3000 3 -Fo .\spirv\vertex.spv

.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T ps_6_0 -E mainPS -spirv -fspv-target-env=vulkan1.1 -fvk-b-shift 0 0 -fvk-u-shift 2000 0 -fvk-t-shift 1000 0 -fvk-s-shift 3000 0 -fvk-b-shift 0 1 -fvk-u-shift 2000 1 -fvk-t-shift 1000 1 -fvk-s-shift 3000 1 -fvk-b-shift 0 2 -fvk-u-shift 2000 2 -fvk-t-shift 1000 2 -fvk-s-shift 3000 2 -fvk-b-shift 0 3 -fvk-u-shift 2000 3 -fvk-t-shift 1000 3 -fvk-s-shift 3000 3 -Fo .\spirv\pixel_blit.spv
.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T vs_6_0 -E mainVS -spirv -fvk-invert-y -fspv-target-env=vulkan1.1 -fvk-b-shift 0 0 -fvk-u-shift 2000 0 -fvk-t-shift 1000 0 -fvk-s-shift 3000 0 -fvk-b-shift 0 1 -fvk-u-shift 2000 1 -fvk-t-shift 1000 1 -fvk-s-shift 3000 1 -fvk-b-shift 0 2 -fvk-u-shift 2000 2 -fvk-t-shift 1000 2 -fvk-s-shift 3000 2 -fvk-b-shift 0 3 -fvk-u-shift 2000 3 -fvk-t-shift 1000 3 -fvk-s-shift 3000 3 -Fo .\spirv\vertex_blit.spv
pause