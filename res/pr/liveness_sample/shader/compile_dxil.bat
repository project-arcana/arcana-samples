.\dxc_bin\dxc.exe .\shader.hlsl -T ps_6_0 -E mainPS -Fo .\dxil\pixel.dxil
.\dxc_bin\dxc.exe .\shader.hlsl -T vs_6_0 -E mainVS -Fo .\dxil\vertex.dxil

.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T ps_6_0 -E mainPS -Fo .\dxil\blit_pixel.dxil
.\dxc_bin\dxc.exe .\shader_blit_fs.hlsl -T vs_6_0 -E mainVS -Fo .\dxil\blit_vertex.dxil
pause