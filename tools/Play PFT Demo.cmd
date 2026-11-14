@echo off
setlocal
if not exist "%~dp0PFTDigitalTwin.exe" (
    echo Play PFT Demo.cmd must be beside PFTDigitalTwin.exe in the Windows package.
    pause
    exit /b 1
)
pushd "%~dp0" || exit /b 1
set "DemoMap=/Game/Level/PFTFirstFloorComplete"
set "DemoArgs=-d3d11 -windowed -ResX=1280 -ResY=720 -nosplash"
set "DemoSettings=r.SetRes 1280x720w,r.VSync 0,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.PostProcessQuality 1,sg.EffectsQuality 1,r.ScreenPercentage 100,t.MaxFPS 30,r.LightMaxDrawDistanceScale 0.5"
if /i "%~1"=="walkthrough" goto checks
if /i "%~1"=="verify" goto verify
if /i "%~1"=="profile" goto profile

start "" "PFTDigitalTwin.exe" %DemoMap% %DemoArgs% -ExecCmds="%DemoSettings%"
popd
exit /b

:checks
call :run_check FirstFloorWalk PFTFirstFloor.WalkPastZone1200 WalkPastZone1200
goto checks_done

:verify
call :run_check CommonsWalk PFTFirstFloor.WalkToCommons WalkToCommons
if errorlevel 1 goto checks_done
call :run_check Navigator PFTNavigator.Panel.AfterOnboarding AfterOnboarding -RenderOffscreen
goto checks_done

:profile
call :run_check CommonsProfile PFTFirstFloor.WalkToCommons WalkToCommons
if errorlevel 1 goto checks_done
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0measure_demo_frames.ps1" -Log "%TestLog%"

:checks_done
set "Result=%errorlevel%"
popd
exit /b %Result%

:run_check
set "TestLog=%~dp0PFTDigitalTwin\Saved\Logs\%~1-%RANDOM%-%RANDOM%.log"
echo Running %~2. Read the log at "%TestLog%".
start "" /wait "PFTDigitalTwin.exe" %DemoMap% %DemoArgs% %~4 -PFTWalkMap=%DemoMap% -ExecCmds="%DemoSettings%,Automation RunTests %~2" -TestExit="Automation Test Queue Empty" -abslog="%TestLog%" -ReportExportPath="%~dp0PFTDigitalTwin\Saved\Automation\%~1"
set "Result=%errorlevel%"
if not "%Result%"=="0" goto check_done
findstr /L /C:"Test Completed. Result={Success} Name={%~3} Path={%~2}" "%TestLog%" >nul
set "Result=%errorlevel%"

:check_done
if not "%Result%"=="0" echo %~2 failed. Check "%TestLog%".
exit /b %Result%
