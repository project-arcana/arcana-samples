.\dxc_bin\dxc.exe .\shader.hlsl -T ps_6_0 -E main_ps -Fo .\bin\pixel.dxil
.\dxc_bin\dxc.exe .\shader.hlsl -T vs_6_0 -E main_vs -Fo .\bin\vertex.dxil

.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T ps_6_0 -E main_ps -Fo .\bin\blit_pixel.dxil
.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T vs_6_0 -E main_vs -Fo .\bin\blit_vertex.dxil

.\dxc_bin\dxc.exe .\mipgen.hlsl -T cs_6_0 -E main_cs -Fo .\bin\mipgen.dxil
.\dxc_bin\dxc.exe .\mipgen_gamma.hlsl -T cs_6_0 -E main_cs -Fo .\bin\mipgen_gamma.dxil
.\dxc_bin\dxc.exe .\mipgen_array.hlsl -T cs_6_0 -E main_cs -Fo .\bin\mipgen_array.dxil

pause