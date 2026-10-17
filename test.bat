@echo off
setlocal
ctest --test-dir "%~dp0build_x64" -C Release --output-on-failure
if errorlevel 1 exit /b 1
call "%~dp0python.bat" "%~dp0tests\texture_images_test.py"
if errorlevel 1 exit /b 1
call "%~dp0python.bat" "%~dp0tests\data_viewer_test.py" --textures
exit /b %errorlevel%
