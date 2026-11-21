@echo off
rem 编译验证（build.bat 的静默版，去掉 pause）。确认删除 Fibersnake 后工程仍能构建。
set "PROJ=D:\C++ prog\ALL VSLZATION V2"
cd /d "%PROJ%"

taskkill /IM MAIN.exe /F >nul 2>&1

call "D:\C++\VC\Auxiliary\Build\vcvars64.bat" >nul
if %ERRORLEVEL% NEQ 0 ( echo VCVARS_FAILED & exit /b 1 )

set "CMAKE=D:\C++\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cd out
"%CMAKE%" .. -DCMAKE_BUILD_TYPE=Release -G "Ninja" >..\out_cfg.log 2>&1
if %ERRORLEVEL% NEQ 0 ( echo CONFIGURE_FAILED & exit /b 1 )

"%CMAKE%" --build . --config Release >..\out_build.log 2>&1
if %ERRORLEVEL% NEQ 0 ( echo BUILD_FAILED & exit /b 1 )

echo BUILD_OK
exit /b 0
