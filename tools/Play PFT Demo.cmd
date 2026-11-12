@echo off
setlocal
if not exist "%~dp0PFTDigitalTwin.exe" (
    echo Play PFT Demo.cmd must be beside PFTDigitalTwin.exe in the Windows package.
    pause
    exit /b 1
)
pushd "%~dp0" || exit /b 1
start "" "PFTDigitalTwin.exe" -d3d11 -windowed -ResX=1280 -ResY=720 -nosplash -ExecCmds="r.SetRes 1280x720w,r.VSync 0,sg.ShadowQuality 1,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.PostProcessQuality 1,sg.EffectsQuality 1,r.ScreenPercentage 100,t.MaxFPS 30"
popd
