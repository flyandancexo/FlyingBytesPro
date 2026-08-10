param(
    [Parameter(Mandatory = $true)][string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'

$LibwdiVersion = '1.5.1'
$LibwdiCommit = '9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602'
$LibwdiRoot = Join-Path $ProjectRoot 'resources\libwdi'

$RequiredFiles = @(
    'include\libwdi.h',
    'lib\libwdi.a',
    'LICENSE.libwdi.txt',
    'VERSION.txt',
    'libwdi-source.zip'
)

foreach ($RelativePath in $RequiredFiles) {
    $Path = Join-Path $LibwdiRoot $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Bundled libwdi file is missing: $Path"
    }
}

$VersionText = Get-Content -LiteralPath (Join-Path $LibwdiRoot 'VERSION.txt') -Raw
if (-not $VersionText.Contains("libwdi $LibwdiVersion") -or
    -not $VersionText.Contains($LibwdiCommit)) {
    throw 'Bundled libwdi version information does not match the pinned FlyingBytesPro dependency.'
}

Write-Host "Using bundled libwdi $LibwdiVersion from resources\libwdi."
