@echo off
rem Roda um comando dentro do ambiente do MSVC x64 (cl.exe, Ninja e CMake do Visual Studio no PATH).
rem Uso: tools\vsdev.cmd cmake --preset win-debug
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo vsdev: Visual Studio com o MSVC x64 nao encontrado 1>&2
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
%*
