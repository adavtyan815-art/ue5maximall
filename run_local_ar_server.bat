@echo off
title MaxiMall Local WebAR Server (Port 8080)
cd /d "%~dp0Saved\AR_Exports"

echo ====================================================================
echo   MaxiMall Local WebAR Server
echo   Serving Directory: %CD%
echo ====================================================================
echo.

where node >nul 2>nul
if %errorlevel% equ 0 (
    echo [OK] Using Node.js server...
    node server.js
    goto :end
)

where python >nul 2>nul
if %errorlevel% equ 0 (
    echo [OK] Using Python HTTP server...
    python -m http.server 8080 --bind 0.0.0.0
    goto :end
)

echo [OK] Using PowerShell HTTP server...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$listener = New-Object System.Net.HttpListener; $listener.Prefixes.Add('http://+:8080/'); $listener.Start(); Write-Host 'Server running on http://+:8080/'; while ($listener.IsListening) { $ctx = $listener.GetContext(); $req = $ctx.Request; $res = $ctx.Response; $path = '.' + $req.RawUrl.Split('?')[0]; if ($path -eq './') { $path = './index.html' }; if (Test-Path $path) { $bytes = [System.IO.File]::ReadAllBytes($path); $res.ContentLength64 = $bytes.Length; $res.Headers.Add('Access-Control-Allow-Origin', '*'); $res.OutputStream.Write($bytes, 0, $bytes.Length); } else { $res.StatusCode = 404 }; $res.Close() }"

:end
pause
