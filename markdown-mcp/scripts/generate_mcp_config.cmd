@echo off
setlocal
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  py -3 "%~dp0generate_mcp_config.py" %*
) else (
  python "%~dp0generate_mcp_config.py" %*
)
set "python_exit_code=%ERRORLEVEL%"
endlocal & exit /b %python_exit_code%
