@echo off
setlocal
python "%~dp0run_headless_integration.py" %*
set "python_exit_code=%ERRORLEVEL%"
endlocal & exit /b %python_exit_code%
