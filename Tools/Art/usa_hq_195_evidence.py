"""Label/crop actual Unreal captures; no synthesized pixels or retouching."""
import math,json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageOps
ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq';OUT=ROOT/'renders/issue195/phase_b'
def font(n):return ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',n)
def sheet(name,items,title,cols=2,w=850,h=500,note='Actual Unreal editor captures | manual LOD selection | 60 degree crop'):
    if any(not (OUT/(p+'.png')).exists() for _,p in items):return
    canvas=Image.new('RGB',(cols*w,100+math.ceil(len(items)/cols)*h),(19,25,33));d=ImageDraw.Draw(canvas)
    d.text((22,12),title,font=font(28),fill='white');d.text((22,56),note,font=font(17),fill='#bbc7d7')
    for i,(label,p) in enumerate(items):
        x=i%cols*w;y=100+i//cols*h;d.text((x+20,y+7),label,font=font(21),fill='white')
        im=ImageOps.contain(Image.open(OUT/(p+'.png')).convert('RGB'),(w-28,h-48));canvas.paste(im,(x+(w-im.width)//2,y+44))
    canvas.save(OUT/name)
for p in OUT.glob('*_raw.png'):
    im=Image.open(p);w=round(im.width*math.tan(math.pi/6));h=round(w*9/16);x=(im.width-w)//2;y=(im.height-h)//2
    im.crop((x,y,x+w,y+h)).save(OUT/(p.stem[:-4]+'.png'))
    meta=OUT/(p.stem[:-4]+'.json')
    if meta.exists():
        r=json.loads(meta.read_text());r['review_crop']={'raw_horizontal_fov':90,'effective_horizontal_fov':60,'rect':[x,y,w,h],'aspect':'16:9','resampling':'none'};meta.write_text(json.dumps(r,indent=2))
sheet('Fixtures_Day.png',[(f'{side} floodlight | 35 x 25 x 20 cm housing',f'flood_{side.lower()}_close_day') for side in ('West','East')]+[(f'{side} amber beacon | 15.5 cm base',f'beacon_{side.lower()}_close_day') for side in ('South','North')],'USA HQ #195 Phase B | four additive fixtures')
sheet('Fixtures_Night.png',[(f'{side} floodlight | powered lens',f'flood_{side.lower()}_close_night') for side in ('West','East')]+[(f'{side} amber beacon | flash ON pose',f'beacon_{side.lower()}_close_night') for side in ('South','North')],'USA HQ #195 Phase B | night close-ups',note='Actual Unreal editor | temporary preview lights | flash ON pose, no gameplay wiring')
sheet('RTS_Comparison.png',[(f'{d} m | {label}',f'{kind}_{d}m') for d in (12,150,450) for kind,label in [('master','dense shell reference'),('far' if d==450 else 'game','game assembly')]],'USA HQ #195 Phase B | source master and game views')
sheet('LOD_Switch.png',[('249.6 m | LOD0','game_249p6m'),('270.4 m | LOD1','far_270p4m')],'USA HQ #195 Phase B | 260 m switch pair')
sheet('Day_Night_150m.png',[('150 m | daylight','game_150m'),('150 m | night preview','night_150m')],'USA HQ #195 Phase B | RTS appearance')
sheet('Night_Context.png',[('Command facade | lamps at 15.95 / 17.09 m','night_facade'),('Garage | original blue strips and amber domes','garage_night')],'USA HQ #195 Phase B | illumination context')
