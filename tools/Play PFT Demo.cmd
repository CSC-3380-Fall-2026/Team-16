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
set "DemoSettings=r.SetRes 1280x720w,r.VSync 0,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.PostProcessQuality 1,sg.EffectsQuality 1,r.ScreenPercentage 100,t.MaxFPS 30,r.LightMaxDrawDistanceScale 1"
if /i "%~1"=="walkthrough" goto walkthrough

start "" "PFTDigitalTwin.exe" %DemoMap% %DemoArgs% -ExecCmds="%DemoSettings%"
popd
exit /b

:walkthrough
set "WalkLog=%~dp0PFTDigitalTwin\Saved\Logs\FirstFloorWalk-%RANDOM%-%RANDOM%.log"
start "" /wait "PFTDigitalTwin.exe" %DemoMap% %DemoArgs% -PFTWalkMap=%DemoMap% -ExecCmds="%DemoSettings%,Automation RunTests PFTFirstFloor.WalkPastZone1200" -TestExit="Automation Test Queue Empty" -abslog="%WalkLog%" -ReportExportPath="%~dp0PFTDigitalTwin\Saved\Automation\FirstFloorWalk"
set "Result=%errorlevel%"
if not "%Result%"=="0" goto walkthrough_done
findstr /L /C:"Test Completed. Result={Success} Name={WalkPastZone1200}" "%WalkLog%" >nul
set "Result=%errorlevel%"

:walkthrough_done
if not "%Result%"=="0" echo Walkthrough failed. Check "%WalkLog%".
popd
exit /b %Result%
