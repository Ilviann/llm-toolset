@echo off
setlocal
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  py -3 "%~dp0deploy_plugin_windows.py" %*
) else (
  python "%~dp0deploy_plugin_windows.py" %*
)
set "python_exit_code=%ERRORLEVEL%"
endlocal & exit /b %python_exit_code%
