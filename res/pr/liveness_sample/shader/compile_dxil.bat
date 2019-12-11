.\dxc_bin\dxc.exe .\shader.hlsl -T ps_6_0 -E main_ps -Fo .\dxil\pixel.dxil
.\dxc_bin\dxc.exe .\shader.hlsl -T vs_6_0 -E main_vs -Fo .\dxil\vertex.dxil

.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T ps_6_0 -E main_ps -Fo .\dxil\blit_pixel.dxil
.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T vs_6_0 -E main_vs -Fo .\dxil\blit_vertex.dxil
pause