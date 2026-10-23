"""08 review-only reflection cards for vertical moulded glass highlights."""
import bpy
from wall_lamp_render import aim

def cards():
    for name,pos,power in [('REVIEW glass left',(.08,-.4,.245),.25),('REVIEW glass right',(.12,.4,.245),.18)]:
        if bpy.data.objects.get(name):continue
        d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='RECTANGLE';d.size=.028;d.size_y=.23
        o=bpy.data.objects.new(name,d);bpy.context.scene.collection.objects.link(o);o.location=pos;aim(o,(0,0,.245))
