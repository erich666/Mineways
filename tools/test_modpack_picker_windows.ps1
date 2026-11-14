# Run on a disposable Windows runner. Only the spawned Mineways process is controlled.
param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [Parameter(Mandatory = $true)][string]$Installations,
    [Parameter(Mandatory = $true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$Output = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($Output) | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public static class ModpackPickerWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    public delegate bool EnumWindowCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint id, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
        UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
    public static string ClassName(IntPtr window) {
        var name = new StringBuilder(256); GetClassName(window, name, name.Capacity); return name.ToString();
    }
    public static IntPtr[] Windows(int process) {
        var windows = new List<IntPtr>();
        EnumWindows((window, parameter) => {
            uint id; GetWindowThreadProcessId(window, out id);
            if (id == process) windows.Add(window);
            return true;
        }, IntPtr.Zero);
        return windows.ToArray();
    }
    public static string Title(IntPtr window) {
        var text = new StringBuilder(2048); GetWindowText(window, text, text.Capacity); return text.ToString();
    }
}
'@
function Save-WindowDiagnostics {
    $native = @([ModpackPickerWindow]::Windows($process.Id) | ForEach-Object {
        @{ handle = $_.ToInt64(); title = [ModpackPickerWindow]::Title($_);
           class = [ModpackPickerWindow]::ClassName($_); enabled = [ModpackPickerWindow]::IsWindowEnabled($_) }
    })
    $uia = @()
    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
    $items = [Windows.Automation.AutomationElement]::RootElement.FindAll(
        [Windows.Automation.TreeScope]::Children, $condition)
    foreach ($item in $items) {
        $uia += @{ name = $item.Current.Name; class = $item.Current.ClassName;
                   handle = $item.Current.NativeWindowHandle; process = $item.Current.ProcessId }
    }
    @{ native_windows = $native; uia_windows = $uia; process_id = $process.Id } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'picker-windows.json') -Encoding utf8
    Write-Host ($native | ConvertTo-Json -Compress)
}
function Send-Menu([int]$Command) {
    if ([ModpackPickerWindow]::GetMenuState([ModpackPickerWindow]::GetMenu($window), $Command, 0) -eq [uint32]::MaxValue) {
        Save-WindowDiagnostics
        throw "The packaged main window has no menu command $Command"
    }
    if (-not [ModpackPickerWindow]::PostMessage($window, 0x111, [IntPtr]$Command, [IntPtr]::Zero)) {
        throw "Cannot post menu command $Command to the Mineways main window"
    }
}
function Find-Dialog([string]$Title, [int]$Seconds = 45) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Mineways exited: $($process.ExitCode)" }
        $condition = [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::ProcessIdProperty, $process.Id)
        $windows = [Windows.Automation.AutomationElement]::RootElement.FindAll(
            [Windows.Automation.TreeScope]::Children, $condition)
        foreach ($item in $windows) { if ($item.Current.Name -eq $Title) { return $item } }
        foreach ($candidate in [ModpackPickerWindow]::Windows($process.Id)) {
            if ([ModpackPickerWindow]::Title($candidate) -eq $Title) {
                return [Windows.Automation.AutomationElement]::FromHandle($candidate)
            }
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    Save-WindowDiagnostics
    throw "Missing native dialog: $Title. See picker-windows.json and picker-execution.log."

}
function Invoke-Button($Dialog, [string]$Id) {
    $button = $Dialog.FindFirst([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id))
    if ($null -eq $button) { throw "Native dialog button $Id was not found" }
    $button.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function Select-Path([string]$Title, [string]$Path, [string]$FieldName = 'Folder') {
    $dialog = Find-Dialog $Title
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        # The world picker starts in saves. Select its visible world item directly.
        if ($Title -eq "Choose a World in This Modpack's saves Folder") {
            $items = $dialog.FindAll([Windows.Automation.TreeScope]::Descendants,
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,
                    [IO.Path]::GetFileName($Path)))
            foreach ($item in $items) {
                $selection = $null
                if ($item.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern, [ref]$selection)) {
                    $selection.Select()
                    Invoke-Button $dialog '1'
                    return
                }
            }
        }
        # Shell dialog controls appear after the top-level dialog. Wait for their providers.
        $field = $dialog.FindFirst([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, '1148'))
        $value = $null
        if ($null -ne $field) {
            [void]$field.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern, [ref]$value)
            if ($null -eq $value) {
                $children = $field.FindAll([Windows.Automation.TreeScope]::Descendants,
                    [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,
                        [Windows.Automation.ControlType]::Edit))
                foreach ($child in $children) {
                    $candidate = $null
                    if ($child.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern, [ref]$candidate) -and
                        -not $candidate.Current.IsReadOnly) { $value = $candidate; break }
                }
            }
        }
        if ($null -eq $value) {
            $edits = $dialog.FindAll([Windows.Automation.TreeScope]::Descendants,
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,
                    [Windows.Automation.ControlType]::Edit))
            foreach ($edit in $edits) {
                $candidate = $null
                if ($edit.Current.Name -match ('^' + [regex]::Escape($FieldName)) -and
                    $edit.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern, [ref]$candidate) -and
                    -not $candidate.Current.IsReadOnly) { $value = $candidate; break }
            }
        }
        if ($null -ne $value -and -not $value.Current.IsReadOnly) {
            $value.SetValue($Path)
            Invoke-Button $dialog '1'
            return
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    $controls = @($dialog.FindAll([Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.Condition]::TrueCondition) | ForEach-Object {
            @{ name = $_.Current.Name; id = $_.Current.AutomationId; class = $_.Current.ClassName;
               type = $_.Current.ControlType.ProgrammaticName }
        })
    $controls | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Output 'picker-controls.json') -Encoding utf8
    Save-WindowDiagnostics
    throw "Native picker has no editable $FieldName field or world item: $Title. See picker-controls.json."
}
function Wait-Ready([string]$WorldName, [int]$Seconds = 1200) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Mineways exited: $($process.ExitCode)" }
        $title = [ModpackPickerWindow]::Title($window)
        if ([ModpackPickerWindow]::IsWindowEnabled($window) -and $title -match '^Mineways:' -and
            $title -match [regex]::Escape($WorldName)) { return $title }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The normal modpack picker did not complete'
}
$process = $null
try {
    $startup = Join-Path $Output 'picker-startup.mwscript'
    @('Show informational: script', 'Show warning: script', 'Show error: script',
      'Minecraft world: [Block Test World]') | Set-Content -LiteralPath $startup -Encoding utf8
    $execution = Join-Path $Output 'picker-execution.log'
    $process = Start-Process -FilePath (Join-Path $PackageRoot 'Mineways.exe') `
        -ArgumentList ('-s none -l "' + $execution + '" "' + $startup + '"') -WorkingDirectory $PackageRoot -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        $window = [IntPtr]::Zero
        foreach ($candidate in [ModpackPickerWindow]::Windows($process.Id)) {
            if ([ModpackPickerWindow]::ClassName($candidate) -eq 'MINEWAYS' -and
                [ModpackPickerWindow]::Title($candidate) -match '^Mineways: \[Block Test World\]') {
                $window = $candidate
                break
            }
        }
    } until ($window -ne [IntPtr]::Zero -or $process.HasExited -or [DateTime]::UtcNow -gt $deadline)
    if ($window -eq [IntPtr]::Zero) {
        Save-WindowDiagnostics
        throw 'Mineways did not finish startup in its MINEWAYS main window'
    }
    $response = [UIntPtr]::Zero
    if (-not [ModpackPickerWindow]::IsWindowEnabled($window) -or
        [ModpackPickerWindow]::SendMessageTimeout($window, 0, [UIntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$response) -eq [IntPtr]::Zero) {
        Save-WindowDiagnostics
        throw 'Mineways main window is disabled or unresponsive after startup'
    }
    $initial = [ModpackPickerWindow]::Title($window)
    Send-Menu 49003
    Invoke-Button (Find-Dialog 'Choose Modpack Folder') '2'
    Start-Sleep -Milliseconds 500
    if ([ModpackPickerWindow]::Title($window) -ne $initial) { throw 'Picker cancellation changed the scene' }
    $evidence = @()
    $setups = @(Get-ChildItem -LiteralPath $Installations -Filter installation.json -Recurse)
    if ($setups.Count -ne 2) { throw 'Expected both pinned real launcher installations' }
    foreach ($setupFile in $setups) {
        $setup = Get-Content -Raw -LiteralPath $setupFile.FullName | ConvertFrom-Json
        $destination = Join-Path $Output $setupFile.Directory.Name
        [IO.Directory]::CreateDirectory($destination) | Out-Null
        Send-Menu 49003
        Select-Path 'Choose Modpack Folder' $setup.instance
        Select-Path "Choose a World in This Modpack's saves Folder" $setup.world
        $title = Wait-Ready ([IO.Path]::GetFileName($setup.world))
        if ($title -notmatch [regex]::Escape([IO.Path]::GetFileName($setup.world))) {
            throw "The selected world did not load: $title"
        }
        # Cancel the world chooser and reject invalid NBT before pack preparation.
        Send-Menu 49003
        Select-Path 'Choose Modpack Folder' $setup.instance
        Invoke-Button (Find-Dialog "Choose a World in This Modpack's saves Folder") '2'
        Start-Sleep -Milliseconds 250
        if ([ModpackPickerWindow]::Title($window) -ne $title) { throw 'World chooser cancellation changed the scene' }
        $badWorld = Join-Path ([IO.Path]::GetDirectoryName($setup.world)) ('Invalid picker fixture ' + [guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($badWorld) | Out-Null
        try {
            [IO.File]::WriteAllText((Join-Path $badWorld 'level.dat'), 'Not NBT')
            Send-Menu 49003
            Select-Path 'Choose Modpack Folder' $setup.instance
            Select-Path "Choose a World in This Modpack's saves Folder" $badWorld
            $failure = Find-Dialog 'Choose Modpack Folder'
            $text = $failure.FindAll([Windows.Automation.TreeScope]::Descendants,
                [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,
                    [Windows.Automation.ControlType]::Text))
            $messages = @($text | ForEach-Object { $_.Current.Name }) -join ' '
            if ($messages -notmatch 'not a readable Java Anvil world') { throw "Missing actionable invalid-world error: $messages" }
            Invoke-Button $failure '1'
            Start-Sleep -Milliseconds 250
            if ([ModpackPickerWindow]::Title($window) -ne $title) { throw 'Invalid world changed the current scene' }
        } finally { [IO.Directory]::Delete($badWorld, $true) }
        $script = Join-Path $destination 'current-scene.mwscript'
        # Deliberately omit Mod pack and Minecraft world: only the native picker loads them.
        @('Show informational: script', 'Show warning: script', 'Show error: script',
          "Save log file: $(Join-Path $destination 'current-scene.log')",
          'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
          'Export lesser blocks: YES', 'Individual blocks: YES',
          'File type: Export individual textures to directory individual-textures',
          'Selection location min to max: 0, 64, 0 to 15, 67, 15',
          "Export for Rendering: $(Join-Path $destination 'individual.obj')",
          "Export Schematic: $(Join-Path $destination 'roundtrip.schem')") |
              Set-Content -LiteralPath $script -Encoding utf8
        Send-Menu 4010
        Select-Path 'Import Settings from model or script file' $script 'File name'
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            Start-Sleep -Milliseconds 500
            $process.Refresh()
            if ($process.HasExited) { throw 'Mineways exited during current-scene export' }
        } until ((Test-Path -LiteralPath (Join-Path $destination 'roundtrip.schem')) -or [DateTime]::UtcNow -gt $deadline)
        if (-not (Test-Path -LiteralPath (Join-Path $destination 'roundtrip.schem'))) { throw 'Current-scene export did not complete' }
        $rect = New-Object ModpackPickerWindow+Rect
        if (-not [ModpackPickerWindow]::GetWindowRect($window, [ref]$rect)) { throw 'Cannot read map window bounds' }
        $bitmap = New-Object Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $dc = $graphics.GetHdc()
            try {
                if (-not [ModpackPickerWindow]::PrintWindow($window, $dc, 0)) { throw 'Cannot capture loaded map' }
            } finally { $graphics.ReleaseHdc($dc) }
            $bitmap.Save((Join-Path $destination 'native-picker-map.png'), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
        $evidence += @{ installation = $setupFile.FullName; output = $destination; title = $title; process_id = $process.Id }
        Write-Host "PASS: native modpack menu and current-scene export: $($setupFile.Directory.Name)"
    }
    @{ picker_cancel_preserved_scene = $true; instances = $evidence } | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $Output 'gui-picker.json') -Encoding utf8
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
