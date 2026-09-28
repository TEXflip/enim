import cv2
from PIL import Image
from pathlib import Path

Image.MAX_IMAGE_PIXELS = None
img_path = Path("C:\\Users\\miche\\Projects\\enim\\test_images\\outer_wilds.jpg")
img = Image.open(img_path)
img.save(img_path.with_suffix(".bmp"))