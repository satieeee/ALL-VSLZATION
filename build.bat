@echo off
set "PROJ=D:\C++ prog\ALL VSLZATION V2"
cd /d "%PROJ%"

rem 编译前先关掉可能在跑的 MAIN.exe。它锁着 out\MAIN.exe，链接会以 LNK1104 失败，
rem 而失败之后 exe 会停在旧版本甚至被删掉 —— 表现出来就是"明明改了、跑起来还是老样子"。
taskkill /IM MAIN.exe /F >nul 2>&1

echo [1/4] Setting up VS build environment...
call "D:\C++\VC\Auxiliary\Build\vcvars64.bat"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] vcvars64.bat failed!
    pause
    exit /b 1
)

echo.
echo [2/4] Locating CMake...
set "CMAKE=D:\C++\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo [ERROR] cmake.exe not found at: %CMAKE%
    pause
    exit /b 1
)

echo [3/4] Configuring...
if not exist out mkdir out
cd out
"%CMAKE%" .. -DCMAKE_BUILD_TYPE=Release -G "Ninja"
if %ERRORLEVEL% NEQ 0 (echo CONFIGURE FAILED & pause & exit /b 1)

echo.
echo [4/4] Building MAIN.exe...
"%CMAKE%" --build . --config Release
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ============================================
    echo   BUILD SUCCESS
    echo   EXE: %PROJ%\out\MAIN.exe
    echo ============================================
) else (
    echo.
    echo ============================================
    echo   BUILD FAILED (see errors above)
    echo ============================================
)
pause
