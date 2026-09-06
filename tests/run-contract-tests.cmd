@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /EHsc /std:c++20 /W4 /WX /I"..\external\nvngx_dlss_sdk" nr_before_sr_contract_tests.cpp /Fo"build\nr_before_sr_contract_tests.obj" /Fe"build\nr_before_sr_contract_tests.exe"
if errorlevel 1 exit /b 1
"build\nr_before_sr_contract_tests.exe"
exit /b %errorlevel%
