"""Decode SKP embedded images on the worker thread, including TGA textures."""
from io import BytesIO
from PIL import Image, ImageStat
import colorsys
import struct

CHANNELS = [struct.unpack('<f', struct.pack('<f', v / 255.0))[0] for v in range(256)]


def colorize_image(image, target, shift):
    """Legacy HLS adjustment; validated against original SUTextureWriteToFile."""
    average = tuple(int(x) for x in ImageStat.Stat(image).mean[:3])
    def hls(rgb):
        h, l, s = colorsys.rgb_to_hls(*(CHANNELS[x] for x in rgb))
        return (-1/360 if rgb[0] == rgb[1] == rgb[2] else h), l, s
    ah, al, ass = hls(average)
    th, tl, ts = hls(target)
    dl = tl - al
    gray_average = average[0] == average[1] == average[2]
    if target[0] == target[1] == target[2]:
        dh, ds = 0, 0 if gray_average else -1
    else:
        dh, ds = th - (ah if shift and not gray_average else 0), ts - (0 if gray_average else ass)
    cache, output = {}, bytearray(image.width * image.height * 4)
    for i, pixel in enumerate(image.getdata()):
        rgb = pixel[:3]
        color = cache.get(rgb)
        if color is None:
            h, l, s = hls(rgb)
            values = colorsys.hls_to_rgb(((h if shift else 0) + dh) % 1,
                                        max(.01, min(1, l + dl)), max(0, min(1, s + ds)))
            color = bytes(max(0, min(255, int(v * 255))) for v in values)
            if len(cache) >= 65536:
                cache.clear()
            cache[rgb] = color
        output[4*i:4*i+3] = color
        output[4*i+3] = pixel[3]
    return Image.frombytes('RGBA', image.size, bytes(output))


def decode_material_images(surface):
    for material in surface['materials'].values():
        if not material['image']:
            continue
        with Image.open(BytesIO(material['image'])) as source:
            if source.width * source.height > 32 * 1024 * 1024:
                raise ValueError('Texture image exceeds 32 megapixels')
            rgba = source.convert('RGBA')
            if material['type'] == 2:
                target = tuple(round(v*255) for v in material['rgba'][:3])
                rgba = colorize_image(rgba, target, material['colorize_shift'])
            material['has_alpha'] = rgba.getchannel('A').getextrema()[0] < 255
            material['size'] = rgba.size
            # The file uses top-left image rows; OpenGL UV origin is bottom-left.
            material['pixels'] = rgba.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    return surface
