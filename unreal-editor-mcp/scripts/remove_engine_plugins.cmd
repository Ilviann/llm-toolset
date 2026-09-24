@echo off
setlocal DisableDelayedExpansion
if not defined UE58 (
  echo UE58 is not set.
  exit /b 1
)
for %%E in ("%UE58%") do set "engine_root=%%~fE"
if not exist "%engine_root%\Engine\Build\Build.version" (
  echo UE58 does not point to an Unreal Engine installation.
  exit /b 1
)
set "result=0"
for %%D in ("%engine_root%\Engine\Plugins" "%engine_root%\Engine\Plugins\Marketplace") do (
  for %%P in (UnrealMCP UnrealMCPGAS UnrealMCPCommonUI UnrealMCPEnhancedInput UnrealMCPAI UnrealMCPTestCompanion) do (
    if exist "%%~D\%%P\" (
      echo Removing "%%~D\%%P"
      rmdir /s /q "%%~D\%%P"
      if exist "%%~D\%%P\" set "result=1"
    )
  )
)
pause
exit /b %result%
