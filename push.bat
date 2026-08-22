@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0"
title Push ALL VSLZATION to GitHub

echo ============================================
echo   Push ALL VSLZATION to GitHub
echo ============================================
echo.

REM ============================================================
REM  第一步：确认仓库地址（只用填一次，会记住）
REM  在 GitHub 网页新建一个空的仓库（不要勾 README），
REM  复制它的地址，例如：https://github.com/你的名字/ALL-VSLZATION.git
REM ============================================================
set "REPO="

if defined REPO goto have_repo
if exist ".git\.remote-url.txt" (
    set /p REPO=<".git\.remote-url.txt"
)
if defined REPO goto have_repo

set /p REPO=GitHub 仓库地址 (例如 https://github.com/user/ALL-VSLZATION.git):
if not defined REPO (
    echo [错误] 未输入地址，取消推送。
    pause
    exit /b 1
)
echo %REPO%>".git\.remote-url.txt"
:have_repo

echo 仓库地址: %REPO%
echo.

REM ============================================================
REM  第二步：配置 remote
REM ============================================================
git remote remove origin >nul 2>&1
git remote add origin "%REPO%"
if errorlevel 1 (
    echo [错误] 添加 remote 失败，请检查地址。
    pause
    exit /b 1
)

REM ============================================================
REM  第三步：如果远程仓库已有内容（比如网页建的仓库带 README），
REM  先合并再推送，避免被拒绝。
REM ============================================================
echo 检查远程仓库状态...
git fetch origin >nul 2>&1
git rev-parse --verify origin/master >nul 2>&1
if not errorlevel 1 (
    echo 远程仓库已有提交，先合并远端（允许无共同历史）。
    git pull origin master --allow-unrelated-histories --no-edit
    if errorlevel 1 (
        echo [提示] 合并可能产生冲突，请手动处理后再试。
        pause
        exit /b 1
    )
)

REM ============================================================
REM  第四步：推送
REM ============================================================
echo 正在推送...
git push -u origin master
if errorlevel 1 (
    echo.
    echo [错误] 推送失败。常见原因：
    echo   - 没登录 GitHub（会弹出窗口，确认即可）
    echo   - 仓库地址写错
    echo   - 网络问题
    pause
    exit /b 1
)

echo.
echo ============================================
echo   PUSH SUCCESS - 已推送到 GitHub！
echo ============================================
echo  验收: 到网页刷新仓库，应能看到全部源码 +
echo        out/MAIN.exe。下载 zip 解压后双击
echo        out\MAIN.exe 即可体验。
echo ============================================
pause
