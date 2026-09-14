Param(
  [Parameter(Mandatory)]
  [string]$Version
)

$ISCC = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
$deps = @('7z', $ISCC) | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }
if ($deps) {
    Write-Error "Missing required tools: $($missing -join ', ')"
    exit 1
}


##### 1. Build FlashGBX #####

Remove-Item -Recurse -force output,cache,artifacts,setup -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path output, cache, embedded-python,artifacts,setup | Out-Null

$url = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
if (-not (Test-Path "cache\python.zip")) {
    Invoke-WebRequest $url -OutFile "cache\python.zip"
    Expand-Archive "cache\python.zip" -DestinationPath "cache\embedded-python"
}
$py = Get-ChildItem "cache\embedded-python" -Filter python.exe -Recurse | Select-Object -First 1

if (-not (Test-Path "cache\embedded-python\get-pip.py")) {
    Invoke-WebRequest "https://bootstrap.pypa.io/get-pip.py" -OutFile "cache\embedded-python\get-pip.py"
    & "$($py.FullName)" "cache\embedded-python\get-pip.py"
    
    $pth = Get-ChildItem "cache\embedded-python" -Filter "*._pth" | Select-Object -First 1
    @"
python312.zip
.
..
import site
"@ | Set-Content -Path $pth.FullName -Encoding ASCII
}

& "$($py.FullName)" -m pip install Pillow==12.1.0 PySide6==6.10.2 pyserial==3.5 python-dateutil==2.9.0.post0 requests==2.32.5 packaging==26.0 --no-cache-dir
# LK_Chromatic native components
& "$($py.FullName)" -m pip install scikit-build-core==1.0.3 cmake==4.3.4 ninja==1.11.1.4 --no-cache-dir
& "$($py.FullName)" -m pip install . --no-build-isolation --no-deps

# Cleanup PySide6 bloat
Get-ChildItem "cache\embedded-python" -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
Get-ChildItem "cache\embedded-python" -Recurse -Filter "*.pyc" | Remove-Item -Force
@(
    "cache\embedded-python\Lib\site-packages\PySide6\examples",
    "cache\embedded-python\Lib\site-packages\PySide6\include",
    "cache\embedded-python\Lib\site-packages\PySide6\typesystems",
    "cache\embedded-python\Lib\site-packages\PySide6\support",
    "cache\embedded-python\Lib\site-packages\PySide6\translations",
    "cache\embedded-python\Lib\site-packages\PySide6\resources",
    "cache\embedded-python\Lib\site-packages\PySide6\qml",
    "cache\embedded-python\Lib\site-packages\PySide6\lib",
    "cache\embedded-python\Scripts"
) | Where-Object { Test-Path $_ } | Remove-Item -Recurse -Force
Get-ChildItem "cache\embedded-python\Lib\site-packages\PySide6" -Recurse -Include *.exe | Remove-Item -Force
@( "*WebEngine*", "*Multimedia*", "*SpatialAudio*", "*Bluetooth*", "*RemoteObjects*", "*Pdf*", "*Charts*", "*Graphs*", "*DataVisualization*", "*Location*", "*3D*", "*Quick*", "*Qml*", "*Network*", "*Designer*", "*Shader*", "*HttpServer*", "*Qt6Labs*", "*Nfc*", "*OpenGL*", "*Print*", "*Sensors*", "*avutil*", "*avcodec*", "*avformat*", "*opengl32sw*", "*assimp*" ) | ForEach-Object {
    Get-ChildItem "cache\embedded-python\Lib\site-packages\PySide6" -Recurse -Filter $_ | Remove-Item -Recurse -Force
}
@( "cmake*", "ninja*", "scikit-build-core*" ) | ForEach-Object {
    Get-ChildItem "cache\embedded-python\Lib\site-packages\" -Recurse -Filter $_ | Remove-Item -Recurse -Force
}

##### 2. Build launcher #####
Copy-Item -Path ".github\build\Windows\Launcher\*" -Destination . -Force
nmake -f Makefile.nmake all

##### 3. Copy files #####
Move-Item "launcher.exe" "output\FlashGBX.exe" -Force
Copy-Item LICENSE, CHANGES.md, README.md, "Third Party Notices.md" output\
Copy-Item cache\embedded-python output\Python -Recurse -Exclude "*.cpp","*.hpp","*.c","*.h","*.pdb"
# We don't need to copy FlashGBX/ as we build the 'FlashGBX' module into embedded-python, as we needed the DLL
#    New-Item -ItemType Directory -Force -Path output\FlashGBX | Out-Null
#    Copy-Item FlashGBX output\FlashGBX -Recurse
#    Remove-Item "output\FlashGBX\config" -Recurse -Force -ErrorAction SilentlyContinue
# Keep the debug symbols for the DLL
Copy-Item cache\embedded-python\Lib\site-packages\FlashGBX\_LK_Chromatic.pdb artifacts\

##### 4. Sign FlashGBX binaries #####
Write-Host "Signing FlashGBX.exe and _LK_Chromatic.dll"
signtool sign /fd sha256 /tr http://ts.ssl.com /td sha256 /a `
  output\FlashGBX.exe `
  output\Python\Lib\site-packages\FlashGBX\_LK_Chromatic.dll

##### 5. Build zip #####
7z a -tzip -mx=9 "artifacts\FlashGBX-$($Version.Replace('+','_'))_Windows-x64.zip" ".\output\*"


##### 6. Fetch driver ######
$ch341Dir = "artifacts\drivers\CH341"
New-Item -ItemType Directory -Force -Path $ch341Dir | Out-Null
$driverUrl = "https://www.wch-ic.com/download/file?id=65"
$driverPath = Join-Path $ch341Dir "CH341SER.EXE"
Write-Host "Downloading CH341 driver from $driverUrl..."
Invoke-WebRequest $driverUrl -OutFile $driverPath
Write-Host "Verifying digital signature on CH341 driver..."
$signature = Get-AuthenticodeSignature $driverPath
if ($signature.Status -ne "Valid") {
    Write-Error "Invalid digital signature on CH341SER.EXE!"
    Write-Error "Status: $($signature.Status)"
    if ($signature.SignerCertificate) {
        Write-Error "Signer: $($signature.SignerCertificate.SubjectName.Name)"
    }
    exit 1
}
Write-Host "✓ Driver signature verified successfully"
Write-Host "  Signer: $($signature.SignerCertificate.SubjectName.Name)"

###### 7. Configure innosetup #####
Write-Host "Configuring setup.iss..."
if (Test-Path ".github\build\Windows\setup.iss") {
    Copy-Item ".github\build\Windows\setup.iss" "setup.iss" -Force
}

$resolvedOutputDir = (Resolve-Path output).Path
$resolvedCh341Dir = (Resolve-Path $ch341Dir).Path
$resolvedSetupDir = (Resolve-Path setup).Path

(Get-Content "setup.iss") `
    -replace '<APP_VERSION>', "$Version" `
    -replace '<FILES_DIR>', "$resolvedOutputDir" `
    -replace '<CH341_DIR>', "$resolvedCh341Dir" `
    -replace '<OUTPUT_DIR>', "$resolvedSetupDir" | Set-Content "setup.iss"

$filesToCopy = @("CHANGES.md", "README.md", "LICENSE", "Third Party Notices.md")
foreach ($file in $filesToCopy) {
    $srcFile = "output\$file"
    if (Test-Path $srcFile) {
        Copy-Item $srcFile "setup" -Force
    }
}

$appExePath = "output\FlashGBX.exe"
if (Test-Path $appExePath) {
    Rename-Item -Path $appExePath -NewName "FlashGBX-app.exe" -Force
}


##### 8. Build setup #####
Write-Host "Compiling setup with Inno Setup Compiler..."
& $ISCC "setup.iss"

$rawSetupExe = "setup\Setup_FlashGBX.exe"
$finalSetupExePath = "setup\FlashGBX-${Version}_Windows-x64_Setup.exe"
if (Test-Path $rawSetupExe) {
    Move-Item $rawSetupExe $finalSetupExePath -Force
}

echo "Signing setup..."
signtool sign /fd sha256 /tr http://ts.ssl.com /td sha256 /a $finalSetupExePath

7z a -tzip -mx=3 "artifacts/FlashGBX-$($Version.Replace('+','_'))_Windows-x64_Setup.zip" ".\setup\*"
