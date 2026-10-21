#Requires -Version 7.0

<#
.SYNOPSIS
    Fetches the two Chromatic drivers fredemmott's installer ships, for an unsigned build.

.DESCRIPTION
    Puts two files in -Destination for win32.ps1 to take:

    - GowinUSBCableDriverV5_for_win7+.exe, the Gowin USB cable driver, which
      openFPGALoader needs to load the FPGA. It's distributed only inside
      ChromaticFirmwareUpdater, a PyInstaller executable, so that is
      downloaded and unpacked to get it.
    - chromatic_cartio.cat, the signed catalog for the cartridge IO driver.
      Windows won't install that driver from an unsigned catalog, so it comes
      from the portable zip of fredemmott's release for -Version. It's only
      used if the .inf beside it matches release-scripts/chromatic_cartio.inf
      byte for byte, as the signature covers the .inf.

    The driver installer isn't Authenticode-signed, so it's checked against
    the SHA-256 of the copy ChromaticFirmwareUpdater carried when this was written. The
    catalog's signature is checked.

.PARAMETER Version
    The version in FlashGBX/app.py, such as 5.1+fredemmott.3. fredemmott's
    release for it is tagged with a leading v.

.PARAMETER Destination
    Where to put the two files. Not under cache, which win32.ps1 clears.

.PARAMETER ChromaticFirmwareUpdaterPath
    A ChromaticFirmwareUpdater.exe already on disk. Downloaded when not given.

.EXAMPLE
    ./release-scripts/fetch-drivers.ps1 -Version 5.1+fredemmott.3 -Destination drivers
    ./release-scripts/win32.ps1 -Version 5.1+fredemmott.3 -NoSign `
        -GowinDriver 'drivers/GowinUSBCableDriverV5_for_win7+.exe' `
        -CartIOCatalog drivers/chromatic_cartio.cat
#>
[CmdletBinding()]
param (
    [Parameter(Mandatory)]
    [string]$Version,

    [Parameter(Mandatory)]
    [string]$Destination,

    [string]$ChromaticFirmwareUpdaterPath
)

$ErrorActionPreference = 'Stop'

$ChromaticFirmwareUpdaterUrl = 'https://s3.us-east-1.amazonaws.com/updates.modretro.com/apps/' +
    'ChromaticFirmwareUpdater.exe'
$GowinDriverName = 'GowinUSBCableDriverV5_for_win7+.exe'
$GowinDriverSha256 = 'B026197755328AA043FC93B84A6F2AFD57F6AFBE9C8AE4B9106AD18F8AA085B9'
$ReleasesApi = 'https://api.github.com/repos/fredemmott/FlashGBX/releases/tags/'

function Assert-Signed {
    <#
    .SYNOPSIS
        Internal: Throws unless a file carries a valid Authenticode signature.
    #>
    param (
        [Parameter(Mandatory)]
        [string]$Path
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid') {
        throw "$Path isn't validly signed: $($signature.Status)"
    }
    Write-Host "  signed by $($signature.SignerCertificate.Subject)"
}

$null = New-Item -ItemType Directory -Force -Path $Destination
$workspace = Join-Path ([IO.Path]::GetTempPath()) ('flashgbx-drivers-' + (New-Guid).Guid)
$null = New-Item -ItemType Directory -Force -Path $workspace

try {
    Write-Host 'Gowin USB cable driver, from ChromaticFirmwareUpdater'
    if (-not $ChromaticFirmwareUpdaterPath) {
        $ChromaticFirmwareUpdaterPath = Join-Path $workspace 'ChromaticFirmwareUpdater.exe'
        Invoke-WebRequest -Uri $ChromaticFirmwareUpdaterUrl -OutFile $ChromaticFirmwareUpdaterPath
    }
    & python -m pip install --quiet pyinstxtractor-ng
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not install pyinstxtractor-ng, which unpacks ChromaticFirmwareUpdater.'
    }
    Push-Location $workspace
    try {
        & python -m pyinstxtractor_ng $ChromaticFirmwareUpdaterPath | Out-String | Write-Verbose
        if ($LASTEXITCODE -ne 0) {
            throw "Could not unpack $ChromaticFirmwareUpdaterPath."
        }
    }
    finally {
        Pop-Location
    }
    $driver = Get-ChildItem -Path $workspace -Recurse -File |
        Where-Object Name -EQ $GowinDriverName |
        Select-Object -First 1
    if (-not $driver) {
        throw "ChromaticFirmwareUpdater no longer contains $GowinDriverName."
    }
    $hash = (Get-FileHash -LiteralPath $driver.FullName -Algorithm SHA256).Hash
    if ($hash -ne $GowinDriverSha256) {
        throw ("ChromaticFirmwareUpdater now carries a different $GowinDriverName (SHA-256 $hash). " +
            'Check the new one, then update $GowinDriverSha256 in this script.')
    }
    Write-Host "  SHA-256 $hash, as expected"
    Copy-Item -LiteralPath $driver.FullName -Destination $Destination -Force

    Write-Host "Cartridge IO driver catalog, from fredemmott's v$Version"
    # A shared CI runner's address is usually past the anonymous API rate limit
    $headers = @{}
    if ($env:GITHUB_TOKEN) {
        $headers.Authorization = "Bearer $env:GITHUB_TOKEN"
    }
    $release = Invoke-RestMethod -Headers $headers -Uri ($ReleasesApi + [uri]::EscapeDataString("v$Version"))
    $asset = $release.assets |
        Where-Object { $_.name -like '*_Windows-x64.zip' } |
        Select-Object -First 1
    if (-not $asset) {
        throw "fredemmott's v$Version has no portable Windows zip."
    }
    $zip = Join-Path $workspace $asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip
    $unzipped = Join-Path $workspace 'release'
    Expand-Archive -LiteralPath $zip -DestinationPath $unzipped
    $releaseDriver = Join-Path $unzipped 'Drivers/chromatic_cartio'
    $catalog = Join-Path $releaseDriver 'chromatic_cartio.cat'
    if (-not (Test-Path -LiteralPath $catalog)) {
        throw "fredemmott's v$Version zip has no Drivers/chromatic_cartio/chromatic_cartio.cat."
    }
    $theirs = (Get-FileHash -LiteralPath (Join-Path $releaseDriver 'chromatic_cartio.inf')).Hash
    $ours = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'chromatic_cartio.inf')).Hash
    if ($theirs -ne $ours) {
        throw "chromatic_cartio.inf differs from fredemmott's v$Version, so his catalog doesn't cover it."
    }
    Assert-Signed -Path $catalog
    Copy-Item -LiteralPath $catalog -Destination $Destination -Force
}
finally {
    Remove-Item -LiteralPath $workspace -Recurse -Force -ErrorAction SilentlyContinue
}
