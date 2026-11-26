"""append_payload.py - the launcher with its payload (as tools/linux/release_linux.sh does it inline):

    python -I append_payload.py <payload folder> <launcher.exe> <out.exe> <release>

The payload folder is zipped (bundle/ stored: the compiler zip is compressed already) and appended to the launcher,
followed by the trailer launcher.c's read_trailer looks for: "XRPAYLD1", the zip's offset (8 bytes, LE), the release
(32 bytes, NUL padded).
"""
import glob
import os
import shutil
import struct
import sys
import zipfile


def main(argv):
    stage, launcher, out, release = argv[1:5]
    zp = out + ".zip"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(glob.glob(os.path.join(stage, "**", "*"), recursive=True)):
            if os.path.isfile(p):
                rel = os.path.relpath(p, stage).replace(os.sep, "/")
                z.write(p, rel, zipfile.ZIP_STORED if rel.startswith("bundle/") else zipfile.ZIP_DEFLATED)
    with open(out, "wb") as o:
        with open(launcher, "rb") as f:
            o.write(f.read())
        offset = o.tell()
        with open(zp, "rb") as f:
            shutil.copyfileobj(f, o, 1 << 20)
        o.write(b"XRPAYLD1" + struct.pack("<Q", offset) + release.encode().ljust(32, b"\0"))
    os.remove(zp)
    print("  launcher + payload: %.1f MB" % (os.path.getsize(out) / 2 ** 20))


if __name__ == "__main__":
    main(sys.argv)
