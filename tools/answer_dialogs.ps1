# Fills in the file dialogs Geometry Dash opens during a smoke test with "dialogs": true, as a player would:
# the given path in the file name box, then OK. Says how many dialogs it saw, and whether two were ever open
# at once.
#   pwsh tools/answer_dialogs.ps1 -Path C:\...\file.spcreator
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$TimeoutSeconds = 400
)

Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Dialogs {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
    public static List<IntPtr> Find(uint pid) {
        var found = new List<IntPtr>();
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h) && Class(h) == "#32770") found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // The file name box: an Edit in a ComboBox (the address bar's isn't).
    public static IntPtr NameBox(IntPtr dialog) {
        IntPtr box = IntPtr.Zero;
        EnumChildWindows(dialog, (h, l) => {
            if (Class(h) == "Edit" && Class(GetParent(h)) == "ComboBox") { box = h; return false; }
            return true;
        }, IntPtr.Zero);
        return box;
    }
}
"@
$WM_SETTEXT = 0x000C; $WM_COMMAND = 0x0111; $IDOK = 1

$start = Get-Date
$gd = $null
while (-not $gd -and ((Get-Date) - $start).TotalSeconds -lt 60) {
    $gd = Get-Process GeometryDash -ErrorAction SilentlyContinue | Select-Object -First 1
    Start-Sleep -Milliseconds 250
}
if (-not $gd) { "Geometry Dash didn't start"; exit 1 }

$seen = @{}
$mostAtOnce = 0
while (-not $gd.HasExited -and ((Get-Date) - $start).TotalSeconds -lt $TimeoutSeconds) {
    $open = [Dialogs]::Find([uint32]$gd.Id)
    $mostAtOnce = [Math]::Max($mostAtOnce, $open.Count)
    foreach ($h in $open) {
        if ($seen.ContainsKey($h)) { continue }
        Start-Sleep -Milliseconds 700  # let it finish opening
        $box = [Dialogs]::NameBox($h)
        $seen[$h] = $true
        "Dialog $($seen.Count) open: name box $(if ($box -ne [IntPtr]::Zero) { 'found' } else { 'not found' })"
        if ($box -ne [IntPtr]::Zero) {
            [void][Dialogs]::SendMessage($box, $WM_SETTEXT, [IntPtr]::Zero, $Path)
            [void][Dialogs]::PostMessage($h, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero)
        }
    }
    Start-Sleep -Milliseconds 200
}
"Dialogs seen: $($seen.Count), most open at once: $mostAtOnce"
