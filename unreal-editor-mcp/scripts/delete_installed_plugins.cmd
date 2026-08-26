@echo off
setlocal

if not defined UE57 (
  echo UE57 is not set.
  goto :end
)

set "plugins_dir=%UE57%\Engine\Plugins\Marketplace"
if not exist "%plugins_dir%\" (
  echo Unreal Engine plugin directory not found: "%plugins_dir%"
  goto :end
)

for %%P in (UnrealMCP UnrealMCPGAS UnrealMCPTestCompanion) do (
  if exist "%plugins_dir%\%%P\" (
    echo Deleting "%plugins_dir%\%%P"...
    rmdir /s /q "%plugins_dir%\%%P"
  )
)

:end
pause
