@echo off
REM Setup script for PC1 (Unreal Engine 5.3)
REM Copies UE 5.3-compatible Target.cs files into Source/
set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..
echo [Setup] Copying PC1 UE 5.3 Target profiles to Source/ ...
copy /Y "%SCRIPT_DIR%Targets\PC1_UE53\*.Target.cs" "%PROJECT_DIR%\Source\" >nul
echo [Setup] Done! PC1 Target.cs files configured for Unreal Engine 5.3.
