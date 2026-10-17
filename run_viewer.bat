@echo off
setlocal
call "%~dp0python.bat" "%~dp0viewer\viewer.py" %*
if errorlevel 1 pause
