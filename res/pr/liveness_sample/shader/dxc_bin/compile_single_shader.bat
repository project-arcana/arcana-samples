@echo off
set arg_domain=%1
set arg_shadername=%2
set arg_output=%3
if "%arg_output%"=="" set "arg_output=%arg_shadername%"

:: compile the HLSL to DXIL, this is straightforward
@echo on
.\dxc_bin\dxc.exe .\src\%arg_shadername%.hlsl -T %arg_domain%_6_3 -E main_%arg_domain% -Fo .\bin\%arg_output%.dxil
@echo off

:: compile the HLSL to SPIR-V
:: This looks much worse than it is, arguments to any shader compiliation are:
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

set spirv_additional_flags=
IF "%arg_domain%"=="vs" set "spirv_additional_flags=-fvk-invert-y "
IF "%arg_domain%"=="gs" set "spirv_additional_flags=-fvk-invert-y "
IF "%arg_domain%"=="ds" set "spirv_additional_flags=-fvk-invert-y "

@echo on
.\dxc_bin\dxc.exe .\src\%arg_shadername%.hlsl -T %arg_domain%_6_3 -E main_%arg_domain% -spirv %spirv_additional_flags%-fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo .\bin\%arg_output%.spv
@echo off
