@echo off
rem Usage: build.bat [ewj-debug|ewj-release|ewj-relwithdebinfo]
setlocal EnableDelayedExpansion
set PRESET=%1
if "%PRESET%"=="" set PRESET=ewj-relwithdebinfo
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VSPATH%\VC\Tools\Llvm\x64\bin;%PATH%

rem The linker's response file treats ' as a quote, so a checkout path with an
rem apostrophe breaks linking. Build through a junction instead.
set "SRC=%~dp0"
set "ROOT=%~dp0.."
echo "%SRC%" | find "'" >nul
if not errorlevel 1 (
  set "LINK=%TEMP%\ewj_build_root"
  if exist "!LINK!" rmdir "!LINK!"
  mklink /J "!LINK!" "!ROOT!" >nul || exit /b 1
  set "SRC=!LINK!\ewj\"
)
cd /d "%SRC%"
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
