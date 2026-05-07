@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "CMAKE=D:\Qt\Tools\CMake_64\bin\cmake.exe"
set "WINDEPLOYQT=D:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe"
set "NMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\nmake.exe"
set "CLANGCL=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"

if not exist "%CMAKE%" (
  echo [ERROR] CMake not found: %CMAKE%
  exit /b 1
)
if not exist "%NMAKE%" (
  echo [ERROR] NMake not found: %NMAKE%
  exit /b 1
)
if not exist "%CLANGCL%" (
  echo [ERROR] clang-cl not found: %CLANGCL%
  exit /b 1
)

call :find_mingw
if not defined MINGW_ROOT (
  echo [ERROR] MinGW64 not found. Please put MinGW64 next to this project, or set DLLHACK_MINGW_ROOT.
  echo Tried:
  echo   %ROOT%\MinGW64
  echo   %ROOT%\..\MinGW64
  echo   %USERPROFILE%\Desktop\sus-Dllhack\MinGW64
  echo   %USERPROFILE%\Desktop\test\MinGW64
  echo   %USERPROFILE%\Desktop\MinGW64
  exit /b 1
)

echo [INFO] Project: %ROOT%
echo [INFO] MinGW : %MINGW_ROOT%

taskkill /IM DllHack.exe /F >nul 2>nul
taskkill /IM SUS-DLLHACK.exe /F >nul 2>nul
if exist "%BUILD%\DllHack.exe" del /f /q "%BUILD%\DllHack.exe" >nul 2>nul

"%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G "NMake Makefiles" ^
  -DCMAKE_MAKE_PROGRAM="%NMAKE%" ^
  -DCMAKE_CXX_COMPILER="%CLANGCL%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY ^
  -DDLLHACK_MINGW_ROOT="%MINGW_ROOT%"
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILD%" --config Release
if errorlevel 1 exit /b 1

if exist "%WINDEPLOYQT%" (
  "%WINDEPLOYQT%" "%BUILD%\SUS-DLLHACK.exe"
)

if not exist "%BUILD%\tools\mingw64\bin\gcc.exe" (
  echo [ERROR] Runtime gcc.exe was not packaged: %BUILD%\tools\mingw64\bin\gcc.exe
  echo [ERROR] Check DLLHACK_MINGW_ROOT: %MINGW_ROOT%
  exit /b 1
)

call :clean_optional

echo [OK] Build finished.
echo [OK] Runtime compiler: %BUILD%\tools\mingw64\bin\gcc.exe
exit /b 0

:find_mingw
if defined DLLHACK_MINGW_ROOT if exist "%DLLHACK_MINGW_ROOT%\bin\gcc.exe" set "MINGW_ROOT=%DLLHACK_MINGW_ROOT%"
if defined MINGW_ROOT exit /b 0
if exist "%ROOT%\MinGW64\bin\gcc.exe" set "MINGW_ROOT=%ROOT%\MinGW64"
if defined MINGW_ROOT exit /b 0
if exist "%ROOT%\..\MinGW64\bin\gcc.exe" for %%I in ("%ROOT%\..\MinGW64") do set "MINGW_ROOT=%%~fI"
if defined MINGW_ROOT exit /b 0
if exist "%USERPROFILE%\Desktop\sus-Dllhack\MinGW64\bin\gcc.exe" set "MINGW_ROOT=%USERPROFILE%\Desktop\sus-Dllhack\MinGW64"
if defined MINGW_ROOT exit /b 0
if exist "%USERPROFILE%\Desktop\test\MinGW64\bin\gcc.exe" set "MINGW_ROOT=%USERPROFILE%\Desktop\test\MinGW64"
if defined MINGW_ROOT exit /b 0
if exist "%USERPROFILE%\Desktop\MinGW64\bin\gcc.exe" set "MINGW_ROOT=%USERPROFILE%\Desktop\MinGW64"
exit /b 0

:clean_optional
for %%F in (opengl32sw.dll D3Dcompiler_47.dll Qt6Network.dll Qt6Svg.dll CMakeCache.txt Makefile cmake_install.cmake) do if exist "%BUILD%\%%F" del /f /q "%BUILD%\%%F" >nul 2>nul
for %%D in (translations generic tls networkinformation iconengines imageformats CMakeFiles SUS-DLLHACK_autogen zeroeye_cli_autogen .qt) do if exist "%BUILD%\%%D" rd /s /q "%BUILD%\%%D" >nul 2>nul
exit /b 0
