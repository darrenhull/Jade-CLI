$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('jade-package-isolation-' + [guid]::NewGuid().ToString('N'))
try {
    $scripts = Join-Path $root 'scripts'
    [IO.Directory]::CreateDirectory($scripts) | Out-Null
    foreach ($name in @('package-native-cli.ps1', 'NativeDistribution.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../../scripts/$name") -Destination $scripts
    }
    # Packaging must be usable with only the CLI's own manifest validator.
    # Consumer release-download/update tooling is deliberately absent.
    . (Join-Path $scripts 'package-native-cli.ps1')
    if (!(Get-Command New-NativePackage -ErrorAction SilentlyContinue)) {
        throw 'Native packaging entry point was not loaded.'
    }
    if (Get-Command Update-NativeRelease -ErrorAction SilentlyContinue) {
        throw 'CLI packaging loaded consumer update tooling.'
    }
    Write-Output 'NativeDistribution isolated CLI packaging: PASS'
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    if (!$resolved.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test cleanup path.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
