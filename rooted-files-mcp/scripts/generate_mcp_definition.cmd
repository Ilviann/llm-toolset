@echo off
setlocal
python "%~dp0generate_mcp_definition.py" %*
set "python_exit_code=%ERRORLEVEL%"
endlocal & exit /b %python_exit_code%
