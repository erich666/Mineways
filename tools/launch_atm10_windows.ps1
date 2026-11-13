# Private Python bootstrap. The root .cmd launcher forwards all arguments in $args.
$ErrorActionPreference = 'Stop'
$showcaseRoot = Split-Path -Parent $PSScriptRoot
$stage = $null
try {
    $pins = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'showcase_runtime_pins.json') -Raw | ConvertFrom-Json
    $pin = $pins.runtimes.'windows-x64'
    if ($pin.sha256 -cnotmatch '^[0-9a-f]{64}$' -or ([Uri]$pin.url).Scheme -ne 'https') {
        throw 'Invalid pinned Python runtime metadata'
    }
    $runtimeBase = Join-Path $showcaseRoot '.showcase-runtime'
    $runtime = Join-Path $runtimeBase ('windows-x64-' + $pin.sha256.Substring(0, 12))
    foreach ($cache in @($runtimeBase, $runtime)) {
        if ((Test-Path -LiteralPath $cache) -and
            ((Get-Item -LiteralPath $cache -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Python runtime cache must not be a reparse point'
        }
    }
    $python = Join-Path $runtime 'python.exe'
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        [IO.Directory]::CreateDirectory($runtimeBase) | Out-Null
        $stage = Join-Path $runtimeBase ('.download-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($stage) | Out-Null
        $archive = Join-Path $stage 'runtime.zip'
        Write-Output 'Downloading showcase setup tools...'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $pin.url -OutFile $archive -TimeoutSec 180
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            $source = [IO.File]::OpenRead($archive)
            try { $archiveHash = [BitConverter]::ToString($algorithm.ComputeHash($source)).Replace('-', '').ToLowerInvariant() }
            finally { $source.Dispose() }
        } finally { $algorithm.Dispose() }
        if ((Get-Item -LiteralPath $archive).Length -ne $pin.size -or $archiveHash -cne $pin.sha256) {
            throw 'Python runtime size or SHA-256 does not match its pin'
        }
        $unpacked = Join-Path $stage 'python'
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $unpacked)
        $pth = @(Get-ChildItem -LiteralPath $unpacked -Filter 'python*._pth')
        if ($pth.Count -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $unpacked 'python.exe'))) {
            throw 'Python runtime archive is incomplete'
        }
        Add-Content -LiteralPath $pth[0].FullName -Value '..\..\tools' -Encoding ASCII
        if (-not (Test-Path -LiteralPath $runtime)) {
            Move-Item -LiteralPath $unpacked -Destination $runtime
        }
        if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { throw 'Python runtime cache is incomplete' }
    }
    Push-Location -LiteralPath $showcaseRoot
    try {
        & $python -E -s (Join-Path $PSScriptRoot 'prepare_atm10_showcase.py') @args
        $showcaseExit = $LASTEXITCODE
    } finally { Pop-Location }
    exit $showcaseExit
} catch {
    Write-Host ('ATM10 preparation failed: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if ($null -ne $stage -and (Test-Path -LiteralPath $stage)) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
