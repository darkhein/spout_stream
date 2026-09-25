@echo off
setlocal
rem Compile spout_stream en Release et produit le dossier portable install\ avec Visual Studio (2019/2022, charge "Developpement desktop en C++")

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio introuvable.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Outils C++ de Visual Studio introuvables.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

where cmake >nul 2>nul || set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1

rem Dossier portable, pret a copier sur un autre poste
if exist install rmdir /s /q install
cmake --install build --prefix install >nul || exit /b 1

echo.
echo OK : %~dp0install\spout_stream.exe
