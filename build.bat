@echo off
setlocal
cmake -S "%~dp0." -B "%~dp0build_x64" -G "Visual Studio 16 2019" -A x64
if errorlevel 1 exit /b 1
cmake --build "%~dp0build_x64" --config Release
exit /b %errorlevel%
