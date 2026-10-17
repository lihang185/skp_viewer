"""Image orientation/TGA/alpha and original-DLL HLS golden tests."""
import sys
from io import BytesIO
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'viewer'))
from skp_textures import colorize_image, decode_material_images
image=Image.new('RGBA',(2,2));image.putdata([(255,0,0,255),(0,255,0,0),(0,0,255,128),(255,255,255,255)])
for fmt in ['PNG','TGA']:
 stream=BytesIO();image.save(stream,format=fmt)
 surface={'materials':{1:{'image':stream.getvalue(),'type':1}}}
 decode_material_images(surface);mat=surface['materials'][1]
 assert mat['size']==(2,2) and mat['has_alpha']
 assert mat['pixels']==image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
fixtures=ROOT/'tests/fixtures/colorize'
source=Image.open(fixtures/'source.bmp').convert('RGBA')
for path in list(fixtures.glob('*_texture.png'))+list(fixtures.glob('*_shift.png')):
 target=tuple(map(int,path.name.split('_')[:3]));shift=path.name.endswith('_shift.png')
 output=colorize_image(source,target,shift)
 expected=Image.open(path).convert('RGB')
 error=max(abs(a-b) for a,b in zip(output.convert('RGB').tobytes(),expected.tobytes()))
 assert error <= (1 if shift else 0), (path.name,error)
 assert output.getchannel('A').tobytes()==source.getchannel('A').tobytes()
print('PNG/TGA, image origin, alpha and HLS colorization tests passed')
