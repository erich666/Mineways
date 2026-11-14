& (Join-Path $PSScriptRoot 'run_mod_helper_windows.ps1') -Mode modpack -HelperArgs $args
exit $LASTEXITCODE
