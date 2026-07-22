@echo off
setlocal EnableExtensions

if /I "%~1"=="--help" goto usage
if /I "%~1"=="/?" goto usage

pushd "%~dp0" || exit /b 1

where powershell >nul 2>nul
if errorlevel 1 (
    echo [opencv-minimal] ERROR: powershell was not found in PATH.
    goto fail
)

echo [opencv-minimal] Building minimal OpenCV (core+imgproc+imgcodecs+videoio) for DML backend.
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\build_opencv_minimal.ps1" %*
if errorlevel 1 goto fail

popd
exit /b 0

:fail
set "AIMBOT_EXIT_CODE=%ERRORLEVEL%"
if "%AIMBOT_EXIT_CODE%"=="0" set "AIMBOT_EXIT_CODE=1"
echo [opencv-minimal] Failed with exit code %AIMBOT_EXIT_CODE%.
popd
exit /b %AIMBOT_EXIT_CODE%

:usage
echo Usage: build_opencv_minimal.bat [tools\build_opencv_minimal.ps1 arguments]
echo.
echo Common options:
echo   -OpenCvVersion 4.13.0
echo   -ForceRebuild
echo   -MaxCpuCount 4
echo   -DryRun
echo.
echo Output: sunone_aimbot_2\modules\opencv\build\dml_minimal\
echo After build, run build_dml.bat to use the minimal OpenCV automatically.
exit /b 0
