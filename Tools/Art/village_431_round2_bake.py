"""Recoverable high-to-low normal/AO bake. Reads geometry checkpoint; writes round2.
Master review FBXs are evidence only, never production/game meshes.
"""
import json,sys,math
from pathlib import Path
sys.dont_write_bytecode=True
import bpy,bmesh
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
import village_431_round2 as r
OUT=r.OUT

def select(obj):
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj

def export_group(obj,lod,hulls,path):
    group=bpy.data.objects.new(obj.name+'_LODGroup',None);bpy.context.collection.objects.link(group);group['fbx_type']='LodGroup'
    obj.parent=group;lod.parent=group
    bpy.ops.object.select_all(action='DESELECT')
    for o in [group,obj,lod]+hulls:o.hide_set(False);o.select_set(True)
    bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH','EMPTY'},use_mesh_modifiers=False,mesh_smooth_type='FACE',use_tspace=True,colors_type='NONE',apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='X',axis_up='Z',add_leaf_bones=False,bake_anim=False,use_custom_props=True)
    obj.parent=None;lod.parent=None;bpy.data.objects.remove(group,do_unlink=True)

def main():
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'source/HouseA_round2.blend'))
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=8;scene.cycles.device='CPU'
    scene.render.bake.margin=2;scene.render.bake.use_selected_to_active=True;scene.render.bake.cage_extrusion=.035;scene.render.bake.max_ray_distance=.08
    report={}
    for col in bpy.data.collections:col.hide_viewport=False;col.hide_render=False
    for o in bpy.data.objects:o.hide_set(False)
    objects=[o for o in bpy.data.objects if o.type=='MESH' and not o.name.startswith('UCX_') and not o.name.endswith('_LOD1')]
    for obj in objects:
        name=obj.name
        for o in bpy.data.objects:
            if o.type=='MESH':o.hide_render=True
        obj.hide_render=False;select(obj)
        # Unique, non-overlapping baked channel; UV0 remains world-scale scanned UVs.
        obj.data.uv_layers.new(name='BakeUV');obj.data.uv_layers.active_index=1
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.0015,scale_to_bounds=True);bpy.ops.object.mode_set(mode='OBJECT')
        obj.data.uv_layers.active_index=0
        high=obj.copy();high.data=obj.data.copy();high.name=name+'_MASTER';bpy.context.collection.objects.link(high);high.hide_render=False
        # Scope bevels to stable right-angle edges. Narrow gutter returns and
        # acute broken plates cannot support a 12 mm fillet without slivers.
        bm=bmesh.new();bm.from_mesh(high.data);weights=bm.edges.layers.float.new('bevel_weight_edge')
        for e in bm.edges:
            stable=len(e.link_faces)==2 and 1.4<e.calc_face_angle()<1.75 and e.calc_length()>.2 and min(f.calc_area() for f in e.link_faces)>.10
            # Adjacent masonry strips are one continuous wall, not bevelled blocks.
            # Only the building corners/chimney have exposed masonry arrises.
            if stable and not name.endswith('Rubble') and any(f.material_index==0 for f in e.link_faces):
                mid=(e.verts[0].co+e.verts[1].co)/2
                stable=(abs(abs(mid.x)-5.5)<.02 and abs(abs(mid.y)-4.5)<.02) or (mid.x<-3.05 and mid.y<-.3 and mid.z>7.9)
            e[weights]=1 if stable else 0
        bm.to_mesh(high.data);bm.free()
        select(high);bevel=high.modifiers.new('Master_20mm_edge_chamfer','BEVEL');bevel.width=.02;bevel.segments=1;bevel.use_clamp_overlap=False;bevel.affect='EDGES';bevel.limit_method='WEIGHT'
        bpy.ops.object.modifier_apply(modifier=bevel.name)
        bm=bmesh.new();bm.from_mesh(high.data);bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.001);bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(high.data);bm.free();high.data.update()
        high.data.calc_loop_triangles()
        report[name]={'game_triangles':r.qa(obj)['triangles'],'master_triangles':len(high.data.loop_triangles),'bake':'Cycles selected master to active game; geometric object-space normal; UV1; 2048; 2px margin; 20mm chamfer on exposed stable edges','maps':[]}
        # Bake only geometric master detail. Scanned micro normals are combined
        # once in Unreal, rather than being baked and then added a second time.
        normal_links=[]
        for mat in high.data.materials:
            p=mat.node_tree.nodes.get('Principled BSDF')
            for link in list(p.inputs['Normal'].links):
                normal_links.append((mat,link.from_socket,link.to_socket));mat.node_tree.links.remove(link)
        for kind in ['Normal','AO']:
            img=bpy.data.images.new(name+'_'+kind,width=2048,height=2048,alpha=False);img.colorspace_settings.name='Non-Color'
            for mat in obj.data.materials:
                n=mat.node_tree.nodes.new('ShaderNodeTexImage');n.image=img;mat.node_tree.nodes.active=n
            select(high);obj.select_set(True);bpy.context.view_layer.objects.active=obj
            if kind=='Normal':bpy.ops.object.bake(type='NORMAL',normal_space='OBJECT',uv_layer='BakeUV')
            else:bpy.ops.object.bake(type='AO',uv_layer='BakeUV')
            pixels=np.array(img.pixels[:],dtype=np.float32).reshape(2048,2048,4)
            if kind=='Normal':pixels[:,:,1]=1-pixels[:,:,1] # UE mirrors Blender Y.
            else:
                # Large horizontal floor/attic surfaces bypass AO, including margins.
                uv=obj.data.uv_layers['BakeUV'].data
                for p in obj.data.polygons:
                    if abs(p.normal.z)<.8 or p.area<1.5:continue
                    coords=np.array([uv[i].uv[:] for i in p.loop_indices])*2048
                    x0=max(0,int(coords[:,0].min())-2);x1=min(2048,int(coords[:,0].max())+3)
                    y0=max(0,int(coords[:,1].min())-2);y1=min(2048,int(coords[:,1].max())+3)
                    pixels[y0:y1,x0:x1,:3]=1
            img.pixels.foreach_set(pixels.ravel());img.filepath_raw=str(OUT/'textures'/(name+'_Bake'+kind+'.png'));img.file_format='PNG';img.save()
            report[name]['maps'].append(Path(img.filepath_raw).name)
            print('BAKED',name,kind,flush=True)
        for mat,src,dst in normal_links:mat.node_tree.links.new(src,dst)
        select(obj);r.r1.export([obj]+[o for o in bpy.data.objects if o.name.startswith('UCX_'+name+'_') and not o.name.startswith('UCX_'+name+'_Damaged') and not o.name.startswith('UCX_'+name+'_Rubble')],OUT/'fbx'/(name+'.fbx'))
        high.hide_render=True
        # Persist high density source in a named collection; portable review only.
        col=bpy.data.collections.new(name+'_MASTER');scene.collection.children.link(col)
        for old in list(high.users_collection):old.objects.unlink(high)
        col.objects.link(high)
        select(high);r.r1.export([high],OUT/'source'/(high.name+'.fbx'))
        lod=bpy.data.objects[name+'_LOD1']
        # Transfer the unique bake atlas onto the hand-built LOD with nearest-surface
        # barycentrics. Both LODs use the exact same images and material slots.
        from mathutils.bvhtree import BVHTree
        from mathutils import Vector
        from mathutils.geometry import barycentric_transform
        obj.data.calc_loop_triangles();tris=list(obj.data.loop_triangles)
        tree=BVHTree.FromPolygons([v.co for v in obj.data.vertices],[t.vertices for t in tris],all_triangles=True)
        layer=lod.data.uv_layers.get('BakeUV') or lod.data.uv_layers.new(name='BakeUV')
        for p in lod.data.polygons:
            for li in p.loop_indices:
                co=lod.data.vertices[lod.data.loops[li].vertex_index].co
                point,n,idx,dist=tree.find_nearest(co)
                tri=tris[idx];vs=[obj.data.vertices[i].co for i in tri.vertices]
                uvs=[Vector((*obj.data.uv_layers['BakeUV'].data[i].uv,0)) for i in tri.loops]
                value=barycentric_transform(point,*vs,*uvs)
                layer.data[li].uv=(value.x,value.y)
        lod.data.uv_layers.active_index=0
        r.r1.export([lod],OUT/'fbx'/(lod.name+'.fbx'))
        hulls=[o for o in bpy.data.objects if o.name.startswith('UCX_'+name+'_') and o.name[len('UCX_'+name+'_'):].isdigit()]
        export_group(obj,lod,hulls,OUT/'fbx'/(name+'_WithLODs.fbx'))
        col.hide_viewport=True;col.hide_render=True
    for mat in bpy.data.materials:
        if not mat.use_nodes:continue
        for node in list(mat.node_tree.nodes):
            if node.type=='TEX_IMAGE' and node.image and '_Bake' in node.image.filepath:mat.node_tree.nodes.remove(node)
    for img in bpy.data.images:
        if img.source=='FILE':img.filepath='//../textures/'+Path(img.filepath).name
    for col in bpy.data.collections:
        col.hide_viewport=col.name!='SM_Village_HouseA';col.hide_render=col.name!='SM_Village_HouseA'
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source/HouseA_round2_baked.blend'))
    (OUT/'evidence/bake_receipt.json').write_text(json.dumps(report,indent=2))
    print('ALL BAKES COMPLETE',flush=True)
if __name__=='__main__':main()
