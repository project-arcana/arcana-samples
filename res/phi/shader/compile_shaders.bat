@echo off
call .\..\..\..\bin\dxc-wrapper-standalone.exe -l .\src\shaderlist.txt
if NOT ["%errorlevel%"]==["0"] pause
