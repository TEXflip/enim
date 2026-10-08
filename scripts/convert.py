#!/usr/bin/python3
import sys

from PIL import Image
from pathlib import Path

Image.MAX_IMAGE_PIXELS = None

if __name__ == "__main__":
    img_path = Path(sys.argv[1])
    if not img_path.exists():
        print(f"File {img_path} does not exist.")
        sys.exit(1)
    img = Image.open(img_path)
    img.save(img_path.with_suffix(".bmp"))