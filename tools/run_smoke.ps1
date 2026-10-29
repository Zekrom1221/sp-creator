# Runs a smoke test (smoketest.json in the mod save dir) without Geometry Dash taking the screen, so the
# PC can be used meanwhile; the window can still be opened to watch, and minimized again. The mod installed
# has to be a build with the test in (build.bat smoke): a release build leaves smoketest.json alone.
#   pwsh tools/run_smoke.ps1 -Config '{"level": 109947627, ...}'
#
# While the game loads, its window is moved off the screen: minimized then, the game would load very slowly
# (the mod that keeps it going in the background loads last). Once the test starts (the mod renames
# smoketest.json), the window goes back to its place, minimized, and is left alone from then on.
param(
    [Parameter(Mandatory = $true)][string]$Config,
    [int]$TimeoutSeconds = 560
)

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SmokeWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
$SW_RESTORE = 9
$SW_SHOWMINNOACTIVE = 7      # minimized, without taking the focus from the window that has it
$HWND_BOTTOM = [IntPtr]1
$SWP_NOSIZE = 0x1; $SWP_NOACTIVATE = 0x10
$OFF_SCREEN = -32000

$gd = "C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash"
$saveDir = "$env:LOCALAPPDATA\GeometryDash\geode\mods\zekrom.sp-creator"
# A test that just ended may still be closing the game.
for ($i = 0; $i -lt 60 -and (Get-Process GeometryDash -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
if (Get-Process GeometryDash -ErrorAction SilentlyContinue) {
    "Geometry Dash is already running: not starting a smoke test"
    exit 1
}
$Config | Set-Content -Encoding utf8NoBOM "$saveDir\smoketest.json"

$start = Get-Date
$p = Start-Process "$gd\GeometryDash.exe" -WorkingDirectory $gd -PassThru
$homeRect = $null        # where the window was before it went off the screen
$movedAway = 0
$handedOver = $false
while (-not $p.HasExited -and ((Get-Date) - $start).TotalSeconds -lt $TimeoutSeconds) {
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -ne 0 -and -not $handedOver) {
        if (Test-Path "$saveDir\smoketest.json") {
            # Still loading: keep the window off the screen, behind the others.
            if ([SmokeWindow]::IsIconic($h)) { [void][SmokeWindow]::ShowWindow($h, $SW_RESTORE) }
            $r = New-Object SmokeWindow+RECT
            [void][SmokeWindow]::GetWindowRect($h, [ref]$r)
            if ($r.Left -ne $OFF_SCREEN) {
                if (-not $homeRect) { $homeRect = $r }
                [void][SmokeWindow]::SetWindowPos($h, $HWND_BOTTOM, $OFF_SCREEN, $OFF_SCREEN, 0, 0, $SWP_NOSIZE -bor $SWP_NOACTIVATE)
                $movedAway++
            }
        }
        else {
            # The test has started: back in place, minimized; the mod keeps the game going.
            if ($homeRect) {
                [void][SmokeWindow]::SetWindowPos($h, $HWND_BOTTOM, $homeRect.Left, $homeRect.Top, 0, 0, $SWP_NOSIZE -bor $SWP_NOACTIVATE)
            }
            [void][SmokeWindow]::ShowWindow($h, $SW_SHOWMINNOACTIVE)
            $handedOver = $true
        }
    }
    Start-Sleep -Milliseconds 200
}
if (-not $p.HasExited) {
    "Timed out after $TimeoutSeconds s: Geometry Dash is still running"
    exit 1
}
"Exited with code $($p.ExitCode) after $([int]((Get-Date) - $start).TotalSeconds) s (moved off the screen $movedAway times while loading)"
