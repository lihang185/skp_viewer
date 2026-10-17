@echo off
setlocal
if defined SKP_VIEWER_PYTHON (
  "%SKP_VIEWER_PYTHON%" %*
  exit /b
)
if exist "%~dp0.venv\Scripts\python.exe" (
  "%~dp0.venv\Scripts\python.exe" %*
  exit /b
)
if exist "C:\python\Python38\python.exe" (
  "C:\python\Python38\python.exe" %*
  exit /b
)
py -3-64 %*
exit /b %errorlevel%
