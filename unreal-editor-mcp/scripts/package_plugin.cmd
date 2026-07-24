@echo off
setlocal
python "%~dp0package_plugin.py" %*
set "python_exit_code=%ERRORLEVEL%"
endlocal & exit /b %python_exit_code%
