# Run only on a disposable Windows runner: Mineways saves GUI preferences to HKCU.
param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$Screenshot
)
$ErrorActionPreference = 'Stop'
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$Screenshot = [IO.Path]::GetFullPath($Screenshot)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Screenshot)) | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PreviewWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
        UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
}
'@
$script = Join-Path ([IO.Path]::GetDirectoryName($Screenshot)) 'gui-smoke.mwscript'
@(
    'Show informational: script'
    'Show warning: script'
    'Show error: script'
    "Mod pack: $(Join-Path $PackageRoot 'demo/manifest.mwpack')"
    "Minecraft world: $(Join-Path $PackageRoot 'demo/world')"
    'Selection location min to max: 0, 64, 0 to 15, 67, 15'
    'Focus view: 8, 8'
    'Zoom: 16'
) | Set-Content -LiteralPath $script -Encoding utf8
$process = $null
try {
    $process = Start-Process -FilePath (Join-Path $PackageRoot 'Mineways.exe') `
        -ArgumentList ('"' + $script + '"') -WorkingDirectory $PackageRoot -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw "Packaged GUI exited: $($process.ExitCode)" }
        $window = $process.MainWindowHandle
        $ready = $window -ne [IntPtr]::Zero -and $process.MainWindowTitle -match 'Mineways: world.*Zoom 16'
    } until ($ready -or [DateTime]::UtcNow -ge $deadline)
    if (-not $ready) { throw "Packaged GUI did not load the demo: $($process.MainWindowTitle)" }
    $result = [UIntPtr]::Zero
    if ([PreviewWindow]::SendMessageTimeout($window, 0, [UIntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$result) -eq [IntPtr]::Zero) {
        throw 'Packaged GUI did not respond to WM_NULL'
    }
    Start-Sleep -Milliseconds 500
    $rect = New-Object PreviewWindow+Rect
    if (-not [PreviewWindow]::GetWindowRect($window, [ref]$rect)) { throw 'Cannot read GUI window bounds' }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 300 -or $height -lt 200) { throw 'Packaged GUI window is too small' }
    $bitmap = New-Object Drawing.Bitmap($width, $height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $dc = $graphics.GetHdc()
        try {
            if (-not [PreviewWindow]::PrintWindow($window, $dc, 0)) { throw 'PrintWindow failed' }
        } finally { $graphics.ReleaseHdc($dc) }
        $colors = [Collections.Generic.HashSet[int]]::new()
        for ($y = [int]($height * 0.2); $y -lt $height * 0.8; $y += 10) {
            for ($x = [int]($width * 0.2); $x -lt $width * 0.8; $x += 10) {
                [void]$colors.Add($bitmap.GetPixel($x, $y).ToArgb())
            }
        }
        if ($colors.Count -lt 4) { throw 'GUI capture has no varied map content' }
        $bitmap.Save($Screenshot, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    Write-Output "Packaged GUI loaded the demo and responded. PrintWindow capture: $Screenshot"
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
}
