"""Round 4 independent audit. No generator imports or reliance on its evidence.
Run in a fresh headless Blender process after the generator finishes.
"""
import bpy, bmesh, json, hashlib, re, struct, math
import numpy as np
from pathlib import Path
from io_scene_fbx import parse_fbx

ROOT=Path(__file__).resolve().parents[2]/'Art/USA/buildings/usa_hq/rubble'
SLOTS=['Debris','Scorch','Embers']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def positions(o):return np.array([o.matrix_world@v.co for v in o.data.vertices])

def overlaps(mesh):
    # Separating-axis test, independent of generator's polygon clipping method.
    uv=mesh.uv_layers[0].data
    ts=[(t.material_index,np.array([uv[l].uv[:] for l in t.loops])) for t in mesh.loop_triangles]
    grid={}; counts=[0,0,0]
    for i,(slot,p) in enumerate(ts):
        lo=np.floor(p.min(0)*64).astype(int);hi=np.floor(p.max(0)*64).astype(int)
        cells=[(slot,x,y) for x in range(lo[0],hi[0]+1) for y in range(lo[1],hi[1]+1)]
        candidates=set(j for cell in cells for j in grid.get(cell,[]))
        for j in candidates:
            q=ts[j][1]
            if np.any(np.minimum(p.max(0),q.max(0))-np.maximum(p.min(0),q.min(0))<=1e-9):continue
            edges=np.concatenate((np.roll(p,-1,axis=0)-p,np.roll(q,-1,axis=0)-q))
            axes=np.stack((-edges[:,1],edges[:,0]),axis=1)
            axes/=np.maximum(np.linalg.norm(axes,axis=1)[:,None],1e-30)
            pa=p@axes.T;qa=q@axes.T
            if np.all(np.minimum(pa.max(0),qa.max(0))-np.maximum(pa.min(0),qa.min(0))>1e-8):counts[slot]+=1
        for cell in cells:grid.setdefault(cell,[]).append(i)
    return dict(zip(SLOTS,counts))

def main():
    report={'method':'Fresh Blender process; source opened only for coordinate/material comparison; each FBX imported into factory-empty scene. Independent SAT UV intersection and bmesh normal recalculation. No generator/evidence imports.','meshes':{}}
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'source/HQ_Rubble.blend'))
    source={o.name:positions(o) for o in bpy.context.scene.objects if o.type=='MESH'}
    recipe=json.loads((ROOT/'material_recipe.json').read_text())
    assert recipe['slots']==SLOTS
    material_checks={}
    for slot in SLOTS:
        mat=bpy.data.materials[slot];nodes=mat.node_tree.nodes
        checks={}
        for n in nodes:
            if n.type!='TEX_IMAGE':continue
            p=Path(bpy.path.abspath(n.image.filepath));assert p.is_file()
            kind=next(k for k,v in recipe['maps'][slot]['maps'].items() if v==p.name)
            expected='sRGB' if kind=='BaseColor' else 'Non-Color'
            assert n.image.colorspace_settings.name==expected
            w,h=struct.unpack('>II',p.read_bytes()[16:24]);assert w==h and w<=({'Debris':2048,'Scorch':2048,'Embers':512}[slot])
            assert recipe['texture_import'][kind]['sRGB']==(kind=='BaseColor')
            checks[kind]={'file':p.name,'size':[w,h],'source_color_space':expected,'sha256':digest(p)}
        nm=next(n for n in nodes if n.type=='NORMAL_MAP')
        assert abs(nm.inputs['Strength'].default_value-(.85 if slot=='Debris' else .4))<1e-6
        assert nm.uv_map==('SurfaceDetail' if slot=='Debris' else 'UV0')
        combine=nm.inputs['Color'].links[0].from_node
        inv=combine.inputs[1].links[0].from_node
        assert inv.operation=='SUBTRACT' and inv.inputs[0].default_value==1
        checks['normal_convention']='DX delivered, green inverted once in Blender preview, no UE green flip'
        if slot=='Embers':
            bs=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
            assert bs.inputs['Emission Strength'].default_value==recipe['materials'][slot]['emissive_intensity']==7
            tint=bs.inputs['Emission Color'].links[0].from_node
            assert np.allclose(tint.inputs[2].default_value[:3],[1,.105,.008])
            assert recipe['materials'][slot]['emissive_linear_color']==[1,.105,.008]
            checks['emission']={'linear_color':[1,.105,.008],'intensity':7,'constant':True}
        material_checks[slot]=checks
    report['materials']=material_checks
    for name,src in source.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        path=ROOT/(name+'.fbx');bpy.ops.import_scene.fbx(filepath=str(path))
        objects=list(bpy.context.scene.objects);assert len(objects)==1 and objects[0].type=='MESH'
        o=objects[0];m=o.data;m.calc_loop_triangles();co=positions(o)
        assert [x.name for x in m.materials]==SLOTS
        bounds=np.array([co.min(0),co.max(0)])*100
        assert bounds[0,0]>=-1900 and bounds[1,0]<=1900 and bounds[0,1]>=-2800 and bounds[1,1]<=2800 and bounds[0,2]>=-20 and bounds[1,2]<=800
        assert np.allclose(o.location,(0,0,0))
        # Blender imports centimetre vertex buffers with 0.01 object scale.
        assert np.allclose(o.scale,(.01,.01,.01))
        err=float(np.max(np.abs(co-src))*100);assert err<.002
        count=len(m.loop_triangles);assert count<=(15000 if 'LOD1' in name else 60000)
        deg=sum(t.area<1e-10 for t in m.loop_triangles);assert deg==0
        bm=bmesh.new();bm.from_mesh(m);bm.faces.ensure_lookup_table()
        nonman=sum(not e.is_manifold for e in bm.edges)
        inconsistent=sum(not e.is_contiguous for e in bm.edges if e.is_manifold)
        old=[f.normal.copy() for f in bm.faces]
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        flips=sum(a.dot(f.normal)<.99 for a,f in zip(old,bm.faces))
        assert nonman==inconsistent==flips==0
        bm.free()
        channels={}
        for uv in m.uv_layers:
            bad=0
            for t in m.loop_triangles:
                a,b,c=[uv.data[i].uv for i in t.loops]
                bad+=abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<1e-13
            m.calc_tangents(uvmap=uv.name)
            invalid=sum(not all(math.isfinite(x) for x in (*l.tangent,*l.bitangent)) or abs(l.tangent.length-1)>.001 or abs(l.bitangent.length-1)>.001 or abs(l.tangent.dot(m.corner_normals[l.index].vector))>.001 for l in m.loops)
            assert bad==invalid==0
            channels[uv.name]={'degenerate_triangles':int(bad),'invalid_tangent_frames':invalid}
        assert [u.name for u in m.uv_layers]==['UV0','SurfaceDetail']
        ov=overlaps(m);assert not any(ov.values())
        out=sum(any(v<0 or v>1 for v in u.uv) for u in m.uv_layers[0].data);assert out==0
        root,_=parse_fbx.parse(str(path))
        props=next(e for e in next(e for e in root.elems if e.id==b'GlobalSettings').elems if e.id==b'Properties70')
        units=next(p.props[-1] for p in props.elems if p.props[0]==b'UnitScaleFactor');assert units==1
        geom=next(e for e in next(e for e in root.elems if e.id==b'Objects').elems if e.id==b'Geometry')
        raw=np.array(next(e for e in geom.elems if e.id==b'Vertices').props[0]).reshape(-1,3)
        assert np.max(np.abs(raw[:,0]))>1800
        assert any(e.id==b'LayerElementTangent' for e in geom.elems)
        report['meshes'][name]={'triangles':count,'slots':SLOTS,'triangles_per_slot':{s:sum(t.material_index==i for t in m.loop_triangles) for i,s in enumerate(SLOTS)},'bounds_blender_cm':bounds.tolist(),'origin':list(o.location),'scale':list(o.scale),'max_all_vertex_round_trip_error_cm':err,'degenerate_faces':deg,'nonmanifold_edges':nonman,'inconsistent_edges':inconsistent,'faces_changed_by_outward_normal_recalculation':flips,'UV_channels':channels,'UV0_overlapping_triangle_pairs_per_slot':ov,'UV0_out_of_range_loops':out,'FinishBake':'Absent; no finish bake. SurfaceDetail intentionally tiles as declared.','fbx_unit_scale_factor':units,'fbx_contains_tangents':True,'sha256':digest(path)}
    handoff=(ROOT/'CLAUDE_HANDOFF.md').read_text()
    receipts=re.findall(r'\| `([^`]+)` \| `([0-9a-f]{64})` \|',handoff)
    assert len(receipts)==15
    report['handoff_hashes']={p:digest(ROOT/p)==h for p,h in receipts};assert all(report['handoff_hashes'].values())
    assert '**Debris, Scorch, Embers**' in handoff and '`(1, 0.105, 0.008) * 7 * Emissive.R`' in handoff
    report['all_checks_passed']=True
    (ROOT/'evidence/round4_independent_check.json').write_text(json.dumps(report,indent=2)+'\n')
    print('INDEPENDENT ROUND 4 CHECK PASSED',flush=True)

if __name__=='__main__':main()
