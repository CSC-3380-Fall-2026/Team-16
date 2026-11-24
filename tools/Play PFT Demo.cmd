@echo off
setlocal
if not exist "%~dp0PFTDigitalTwin.exe" (
    echo Play PFT Demo.cmd must be beside PFTDigitalTwin.exe in the Windows package.
    pause
    exit /b 1
)
pushd "%~dp0" || exit /b 1
set "DemoMap=/Game/Level/PFTFirstFloorComplete"
set "DemoWidth=1600"
set "DemoHeight=900"
set "DemoRenderScale=75"
set "DemoPreset=%~1"
if not "%~2"=="" set "DemoPreset=%~2"
if /i "%DemoPreset%"=="compact" (
    set "DemoWidth=1280"
    set "DemoHeight=720"
    set "DemoRenderScale=100"
)
set "DemoArgs=-d3d11 -windowed -ResX=%DemoWidth% -ResY=%DemoHeight% -nosplash -ini:Engine:[ConsoleVariables]:r.ScreenPercentage=%DemoRenderScale%"
set "DemoSettings=r.SetRes %DemoWidth%x%DemoHeight%w,r.VSync 0,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.PostProcessQuality 1,sg.EffectsQuality 1,r.AntiAliasingMethod 2,r.TemporalAA.Upsampling 1,r.MaxAnisotropy 8,r.Tonemapper.Sharpen 0.30"
set "DemoFPS=30"
set "DemoHttp=-ini:Engine:[HTTP]:bEnableHttp=False"
if /i "%~1"=="smooth" set "DemoFPS=45"
if /i "%~1"=="profile" if /i "%~2"=="smooth" set "DemoFPS=45"
if /i "%~1"=="profileguided" if /i "%~2"=="smooth" set "DemoFPS=45"
set "DemoLightDistance=0.5"
if "%DemoFPS%"=="45" set "DemoLightDistance=0.35"
set "DemoSettings=%DemoSettings%,r.LightMaxDrawDistanceScale %DemoLightDistance%,t.MaxFPS %DemoFPS%"
if /i "%~1"=="walkthrough" goto checks
if /i "%~1"=="verify" goto verify
if /i "%~1"=="verifyextended" goto verify
if /i "%~1"=="verifyoffline" goto verify
if /i "%~1"=="crossbuilding" goto crossbuilding
if /i "%~1"=="guidedwalk" goto guidedwalk
if /i "%~1"=="reroute" goto reroute
if /i "%~1"=="profile" goto profile
if /i "%~1"=="profileguided" goto profileguided

if /i "%~1"=="offline" set "DemoArgs=%DemoArgs% %DemoHttp%"
start "" "PFTDigitalTwin.exe" %DemoMap% %DemoArgs% -ExecCmds="%DemoSettings%"
popd
exit /b

:checks
call :run_check FirstFloorWalk PFTFirstFloor.WalkPastZone1200 WalkPastZone1200
goto checks_done

:crossbuilding
call :walk_to_c1007
goto checks_done

:guidedwalk
call :walk_with_guidance
goto checks_done

:reroute
call :check_reroute -RenderOffscreen
goto checks_done

:walk_with_guidance
call :run_check GuidedWalk PFTFirstFloor.GuidedWalk GuidedWalk %~1
if errorlevel 1 exit /b %errorlevel%
call :check_assets
exit /b %errorlevel%

:walk_to_c1007
call :run_check C1007Walk PFTFirstFloor.WalkToC1007 WalkToC1007 %~1
if errorlevel 1 exit /b %errorlevel%
call :check_assets
exit /b %errorlevel%

:check_reroute
call :run_check Reroute PFTFirstFloor.RefocusAfterReroute RefocusAfterReroute %~1
if errorlevel 1 exit /b %errorlevel%
call :check_assets
exit /b %errorlevel%

:verify
if /i "%~1"=="verifyoffline" set "DemoArgs=%DemoArgs% %DemoHttp% -LogCmds="LogHttp Verbose""
call :run_check CommonsWalk PFTFirstFloor.WalkToCommons WalkToCommons -RenderOffscreen
if errorlevel 1 goto checks_done
call :check_assets
if errorlevel 1 goto checks_done
call :run_check Navigator PFTNavigator.Panel.AfterOnboarding AfterOnboarding -RenderOffscreen
if errorlevel 1 goto checks_done
call :check_assets
if errorlevel 1 goto checks_done
call :walk_with_guidance -RenderOffscreen
if errorlevel 1 goto checks_done
if /i "%~1"=="verify" goto checks_done
call :walk_to_c1007 -RenderOffscreen
if errorlevel 1 goto checks_done
call :check_reroute -RenderOffscreen
goto checks_done

:profileguided
set "DemoArgs=%DemoArgs% %DemoHttp% -RenderOffscreen -PFTProfileGuidance"
set "ProfileScenario=Guided Commons walk with map (%DemoFPS% FPS cap)"
call :run_check GuidedProfile-%DemoFPS%FPS PFTFirstFloor.GuidedWalk GuidedWalk
goto profile_report

:profile
set "ProfileScenario=Commons walkthrough"
call :run_check CommonsProfile-%DemoFPS%FPS PFTFirstFloor.WalkToCommons WalkToCommons

:profile_report
if errorlevel 1 goto checks_done
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0measure_demo_frames.ps1" -Log "%TestLog%" -Scenario "%ProfileScenario%"

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

:check_assets
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_demo_assets.ps1" -Log "%TestLog%"
exit /b %errorlevel%
