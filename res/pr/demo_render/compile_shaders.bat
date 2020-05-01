@echo off
call .\..\..\..\bin\dxc-wrapper-standalone.exe .\src\shaderlist.txt
if NOT ["%errorlevel%"]==["0"] pause
