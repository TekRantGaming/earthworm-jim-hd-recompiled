@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VSPATH%\VC\Tools\Llvm\x64\bin;%PATH%
cd /d "%~dp0..\ewj"
cmake --preset ewj-relwithdebinfo -DEWJ_DEV_TOOLS=%1 >nul || exit /b 1
cmake --build --preset ewj-relwithdebinfo || exit /b 1
