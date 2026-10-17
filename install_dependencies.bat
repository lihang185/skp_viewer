@echo off
setlocal
call "%~dp0python.bat" -m pip install -r "%~dp0viewer\requirements.txt"
exit /b %errorlevel%
