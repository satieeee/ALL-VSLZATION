@echo off
rem Same build as _build_noPause.bat, but WITHOUT the leading `taskkill /IM MAIN.exe /F`.
rem Use this one whenever MAIN.exe might be open: if the exe is locked, only the LINK step
rem fails (LNK1104) and nothing else is harmed -- the compile still runs, so this is also
rem the way to answer "does main.cpp compile?" without closing the user's window.
call "D:\C++\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "D:\C++ prog\ALL VSLZATION V2\out"
"D:\C++\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -DCMAKE_BUILD_TYPE=Release -G "Ninja" >nul 2>&1
"D:\C++\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build . --config Release
exit /b %ERRORLEVEL%
