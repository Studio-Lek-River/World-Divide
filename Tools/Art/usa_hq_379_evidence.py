"""Layout actual Unreal captures only; no synthesis or retouching.

Native CaptureViewport uses horizontal 90 degrees. A centred pixel crop to
tan(30)/tan(45) yields horizontal 60 degrees, then a 16:9 vertical crop.
Raw PNGs and camera JSON remain alongside each review image.
"""
import json,math,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageOps
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art/USA/buildings/usa_hq/renders/issue379'
FONT='C:/Windows/Fonts/segoeui.ttf'
def font(n):return ImageFont.truetype(FONT,n)
def panel(im,size):return ImageOps.contain(im,size,Image.Resampling.LANCZOS)
def sheet(path,items,title,cols=2,w=900,h=570):
    if '--partial' in sys.argv and any(not Path(p).exists() for _,p in items):return
    rows=math.ceil(len(items)/cols)
    canvas=Image.new('RGB',(cols*w,100+rows*h),(22,27,33));d=ImageDraw.Draw(canvas)
    d.text((24,16),title,font=font(30),fill='white')
    note='Actual Unreal editor captures | manual LOD selection | centred 60-degree crop'
    if path.endswith(('_switch.png','_review.png')):note='Far shell shows visible panel loss: LOD validation FAILED | mast height choice remains approved'
    d.text((24,58),note,font=font(18),fill='#edba82' if 'FAILED' in note else '#aebaca')
    for i,(label,p) in enumerate(items):
        x=(i%cols)*w;y=100+(i//cols)*h
        d.text((x+18,y+8),label,font=font(22),fill='white')
        im=panel(Image.open(p).convert('RGB'),(w-28,h-55))
        canvas.paste(im,(x+(w-im.width)//2,y+45+(h-55-im.height)//2))
    canvas.save(OUT/path)
def main():
    for p in OUT.glob('*_raw.png'):
        im=Image.open(p);w=round(im.width*math.tan(math.pi/6));h=im.height if '_overview_' in p.name else round(w*9/16)
        assert h<=im.height
        x=(im.width-w)//2;y=(im.height-h)//2
        im.crop((x,y,x+w,y+h)).save(OUT/(p.stem[:-4]+'.png'))
        meta=OUT/(p.stem[:-4]+'.json')
        if meta.exists():
            data=json.loads(meta.read_text());data['review_crop']={'raw_horizontal_fov':90,'effective_horizontal_fov':60,'rect':[x,y,w,h],'aspect':'full vertical context' if '_overview_' in p.name else '16:9','resampling':'none'}
            meta.write_text(json.dumps(data,indent=2))
    for h,e in [(24,21),(28,24),(32,27)]:
        sheet(f'H{h}_reference_comparison.png',[
            ('Approved design reference',ROOT/'docs/art/refs/USA_HQ_Default_Approved.png'),
            (f'Candidate22: core {h} m / east {e} m | overview',OUT/f'H{h}_overview.png')],f'USA HQ mast proportion | option {h} m')
        sheet(f'H{h}_review.png',[(f'{dist} m | {kind}',OUT/f'H{h}_{kind}_{dist}m.png') for dist in (12,150,450) for kind in ('master','game')],f'Option {h} m | dense shell master versus budgeted game assembly')
        sheet(f'H{h}_switch.png',[
            ('249.6 m | LOD0, below 260 m switch',OUT/f'H{h}_game_249p6m.png'),
            ('270.4 m | LOD1, above 260 m switch',OUT/f'H{h}_game_270p4m.png')],f'Option {h} m | switch views at -4% and +4%')
    sheet('Height_Options_150m.png',[(f'Core {h} m / east {e} m',OUT/f'H{h}_game_150m.png') for h,e in [(24,21),(28,24),(32,27)]],'USA HQ | mast height review at 150 m',cols=3,w=700,h=460)
    sheet('Height_Options_Overview.png',[(f'Core {h} m / east {e} m',OUT/f'H{h}_overview.png') for h,e in [(24,21),(28,24),(32,27)]],'USA HQ | mast proportions',cols=3,w=700,h=650)
if __name__=='__main__':main()
