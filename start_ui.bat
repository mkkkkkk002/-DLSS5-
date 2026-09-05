@echo off
cd /d "%~dp0"

where node >nul 2>&1
if errorlevel 1 goto noNode

REM Free port 8777 if an old instance is holding it.
for /f "tokens=5" %%a in ('netstat -ano ^| findstr ":8777" ^| findstr /i "LISTENING"') do taskkill /F /PID %%a >nul 2>&1

echo.
echo ============================================================
echo  DLSS5NR service starting... browser will open automatically.
echo  This window is the service terminal.
echo  Closing this window stops the service and cleans temp files.
echo ============================================================
echo.

REM server_guard.exe hosts node web\server.js --open. It intercepts the
REM console-close event, so the disposable temp dirs (.tmp_uploads /
REM .frame_previews) are cleaned at shutdown instead of at the next startup.
server_guard.exe

echo.
echo Service stopped. Press any key to close this window.
pause >nul
exit /b 0

:noNode
echo.
echo [ERROR] node.exe not found in PATH.
echo Install Node.js and add it to PATH, then try again.
echo.
pause >nul
exit /b 1