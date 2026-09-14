# While upstream uses 3.10, hw_Chromatic.py uses some 3.12 features
# 3.12 also matches Windows
PYTHON=python3.12

if ! command -v $PYTHON &>/dev/null; then
  echo "Could not find $PYTHON"
  exit 1
fi
if [[ -z "$1" || "$1" == "--help"  || "$1" == "-h" ]]; then
  echo "Usage: $0 VERSION"
  exit 0
fi

VERSION="$1"

rm -rf \
  venv \
  artifacts \
  dist

$PYTHON -m venv venv
source venv/bin/activate
$PYTHON -m pip install --upgrade pip
$PYTHON -m pip install pyinstaller==6.11.0 Pillow==10.3.0 PySide6==6.7.2 pyserial==3.5 python-dateutil==2.9.0.post0 requests==2.32.3 packaging==25.0
$PYTHON -m pip install scikit-build-core==1.0.3 cmake==4.3.4 ninja==1.11.1.4 --no-cache-dir
$PYTHON -m pip install . --no-build-isolation --no-deps

cat > "./FlashGBX.spec" <<'PYINSTALLER_SPEC'
# -*- mode: python ; coding: utf-8 -*-

hiddenimports = [
    'FlashGBX.hw_GBxCartRW',
    'FlashGBX.hw_GBFlash',
    'FlashGBX.hw_JoeyJr',
    'FlashGBX.hw_GameBub',
    'FlashGBX.hw_Chromatic',
]

a = Analysis(
  ['run.py'],
  pathex=[],
  binaries=[
    ('FlashGBX/res/icon.ico', 'res'),
    ('venv/lib/<PYTHON>/site-packages/FlashGBX/_LK_Chromatic.so', 'FlashGBX'),
  ],
  datas=[],
  hiddenimports=hiddenimports,
  hookspath=[],
  hooksconfig={},
  runtime_hooks=[],
  excludes=[],
  noarchive=False,
)

pyz = PYZ(a.pure)
exe = EXE(
  pyz,
  a.scripts,
  [],
  exclude_binaries=True,
  name='FlashGBX',
  debug=False,
  bootloader_ignore_signals=False,
  strip=False,
  upx=True,
  console=False,
  icon=['FlashGBX/res/icon.ico'],
)
coll = COLLECT(
  exe,
  a.binaries,
  a.datas,
  strip=False,
  upx=True,
  upx_exclude=[],
  name='FlashGBX',
)
info_plist = {
  'CFBundleName': 'FlashGBX',
  'CFBundleDisplayName': 'FlashGBX',
  'CFBundleGetInfoString': 'Interface software for GB/GBC/GBA cart readers',
  'CFBundleShortVersionString': '<APP_VERSION>',
  'CFBundleIdentifier': 'com.lesserkuma.FlashGBX',
}
app = BUNDLE(
  coll,
  name='FlashGBX.app',
  icon='FlashGBX/res/icon.ico',
  bundle_identifier='com.lesserkuma.FlashGBX',
  info_plist=info_plist,
)
PYINSTALLER_SPEC
sed -i '' "s/<APP_VERSION>/${VERSION}/g" "./FlashGBX.spec"
sed -i '' "s/<PYTHON>/${PYTHON}/g" "./FlashGBX.spec"
rm -r FlashGBX/config
pyinstaller FlashGBX.spec
mkdir dist/FlashGBX.app/Contents/MacOS/config
mkdir dist/FlashGBX.app/Contents/MacOS/res
mkdir dist/FlashGBX.app/Contents/MacOS/locale
cp -r FlashGBX/res/* dist/FlashGBX.app/Contents/MacOS/res
cp -r FlashGBX/locale/* dist/FlashGBX.app/Contents/MacOS/locale

# Remove unnecessary files
rm -rf dist/FlashGBX.app/Contents/Resources/PySide6/Qt/lib/Qt{Pdf*,Quick*,Qml*,Network*,OpenGL*}.framework*
rm -rf dist/FlashGBX.app/Contents/Resources/Qt{Pdf*,Quick*,Qml*,Network*,OpenGL*}
rm -rf dist/FlashGBX.app/Contents/Resources/PySide6/Qt/translations
rm -rf dist/FlashGBX.app/Contents/Frameworks/PySide6/Qt/lib/Qt{Pdf*,Quick*,Qml*,Network*,OpenGL*}.framework*
rm -rf dist/FlashGBX.app/Contents/Frameworks/Qt{Pdf*,Quick*,Qml*,Network*,OpenGL*}
rm -rf dist/FlashGBX.app/Contents/Frameworks/PySide6/Qt/translations
find -E dist -type f -regex ".*\.(cpp|hpp|c|h)"


# Build DMG
mkdir -p "dist/dmg"
cp -r "dist/FlashGBX.app" "dist/dmg"

dmg_path="dist/FlashGBX-$(echo "$VERSION" | tr '+' '_')_macOS-$(uname -m).dmg"

max_retries=5
retry_delay=10

for attempt in $(seq 1 $max_retries); do
  if create-dmg \
      --volname "FlashGBX" \
      --volicon "FlashGBX/res/icon.ico" \
      --window-pos 200 120 \
      --window-size 600 300 \
      --icon-size 100 \
      --icon "FlashGBX.app" 175 120 \
      --hide-extension "FlashGBX.app" \
      --app-drop-link 425 120 \
      "$dmg_path" \
      "dist/dmg/"; then
    echo "Successfully created image of FlashGBX v${VERSION}."
    break
  else
    echo "Failed to create DMG (attempt $attempt/$max_retries). Retrying in $retry_delay seconds..."
    sleep $retry_delay
  fi

  if [[ $attempt -eq $max_retries ]]; then
    echo "Error: Failed to create DMG after $max_retries attempts."
    exit 1
  fi
done
