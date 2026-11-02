@echo off
setlocal
title Building Groovebox Pro
cd /d "%~dp0"

echo ============================================================
echo   Groovebox Pro - build script
echo ============================================================

rem ---- find Visual Studio (2026 / 2022) and load the x64 build tools
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio was not found. Install Visual Studio with "Desktop development with C++".
  pause & exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if "%VSINSTALL%"=="" (
  echo The C++ build tools are missing. In the Visual Studio Installer add "Desktop development with C++".
  pause & exit /b 1
)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul

rem ---- optional: Steinberg ASIO SDK for ASIO drivers (falls back to WASAPI if this fails)
if not exist "asiosdk\common\iasiodrv.h" (
  echo Downloading the Steinberg ASIO SDK for ASIO driver support...
  curl -L -s -o asiosdk.zip "https://www.steinberg.net/asiosdk" && (
    powershell -NoProfile -Command "Expand-Archive -Force 'asiosdk.zip' 'asiosdk_tmp'" && (
      for /d %%d in (asiosdk_tmp\*) do if exist "%%d\common\iasiodrv.h" move "%%d" "asiosdk" >nul
    )
  )
  if exist asiosdk_tmp rmdir /s /q asiosdk_tmp
  if exist asiosdk.zip del asiosdk.zip
  if exist "asiosdk\common\iasiodrv.h" (echo ASIO SDK ready.) else (echo ASIO SDK not available - Groovebox Pro will use WASAPI.)
)

rem ---- configure + build (JUCE is downloaded automatically the first time)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo. & echo CMake configure failed. & pause & exit /b 1 )
cmake --build build --config Release
if errorlevel 1 ( echo. & echo Build failed - see the errors above. & pause & exit /b 1 )

rem ---- copy the app next to this folder and make a desktop shortcut
set "EXE=build\GrooveboxPro_artefacts\Release\Groovebox Pro.exe"
copy /y "%EXE%" "..\Groovebox Pro.exe" >nul
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Groovebox Pro.lnk'); $s.TargetPath=(Resolve-Path '..\Groovebox Pro.exe').Path; $s.WorkingDirectory=(Resolve-Path '..').Path; $s.Save()"

echo.
echo ============================================================
echo   Done!  Groovebox Pro.exe is in Documents\Groovebox Pro
echo   and there is a shortcut on your desktop.
echo ============================================================
pause
