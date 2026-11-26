#!/bin/sh
# All icon files from assets/icon.svg (ImageMagick): Windows .ico (16..256), Linux PNGs, Android mipmaps + the
# adaptive icon's foreground (432 px, the car and road in the 66 % safe zone) and background colour.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
svg=$here/assets/icon.svg
out=$here/assets
r() { magick -background none -density 384 "$svg" -resize "$1x$1" "$2"; }
for n in 16 32 48 64 128 256 512; do r $n "$out/icon_$n.png"; done
# the Linux launcher's window icon (launcher/CMakeLists.txt GTA2_LAUNCHER_ICON): 64x64, 32-bit with alpha
magick "$out/icon_64.png" -define bmp:format=bmp4 -type TrueColorAlpha "$out/icon_64.bmp"
magick "$out/icon_16.png" "$out/icon_32.png" "$out/icon_48.png" "$out/icon_64.png" "$out/icon_128.png" \
       "$out/icon_256.png" "$out/icon.ico"
a=$out/android
for d in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
    mkdir -p "$a/mipmap-${d%%:*}"
    r "${d##*:}" "$a/mipmap-${d%%:*}/ic_launcher.png"
done
# adaptive icon: the launcher masks it; the drawing without its own rounded frame, scaled into the safe zone
mkdir -p "$a/drawable-nodpi" "$a/mipmap-anydpi-v26" "$a/values"
magick -background none -density 384 "$svg" -resize 300x300 -gravity center -extent 432x432 "$a/drawable-nodpi/ic_launcher_fg.png"
cat > "$a/mipmap-anydpi-v26/ic_launcher.xml" <<X
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_bg"/>
    <foreground android:drawable="@drawable/ic_launcher_fg"/>
</adaptive-icon>
X
cat > "$a/values/colors.xml" <<X
<?xml version="1.0" encoding="utf-8"?>
<resources><color name="ic_launcher_bg">#0B0A14</color></resources>
X
echo "icons in $out"
