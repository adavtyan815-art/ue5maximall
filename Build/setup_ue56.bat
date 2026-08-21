@echo off
REM Setup script for PC2 (Unreal Engine 5.6 Production Build Machine)
REM Copies proven UE 5.6 production Target.cs files into Source/
set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..
echo [Setup] Copying PC2 UE 5.6 Production Target profiles to Source/ ...
copy /Y "%SCRIPT_DIR%Targets\PC2_UE56\*.Target.cs" "%PROJECT_DIR%\Source\" >nul
echo [Setup] Done! PC2 Target.cs files configured for Unreal Engine 5.6 Production.
