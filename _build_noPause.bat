@echo off
taskkill /IM MAIN.exe /F >nul 2>&1
call "D:\C++\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "D:\C++ prog\ALL VSLZATION V2\out"
"D:\C++\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -DCMAKE_BUILD_TYPE=Release -G "Ninja" >nul 2>&1
"D:\C++\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build . --config Release
exit /b %ERRORLEVEL%
