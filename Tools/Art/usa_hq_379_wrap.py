"""Finalize the selected candidate and handoff receipts after the tangent fix."""
import json
import usa_hq_379_preview as p
p.rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'hq379wrap','version':'1'}})
assert p.call('SceneTools','get_current_level')==f'{p.CAND}/L_HQ379_Masts'
receipt=json.loads((p.WORK/'unreal_tangent_import_check.json').read_text())
assert receipt['triangles']==10655 and not receipt['tangent_warnings']
p.pose(28,'far')
p.capture('H28_game_270p4m',270.4)
p.capture('H28_game_450m',450)
p.counts()
p.pose(28,'game')
p.call('AssetTools','save_assets',asset_paths=[f'{p.CAND}/L_HQ379_Masts'])
repair=json.loads((p.WORK/'tangent_repair.json').read_text())
repair.update(total_uv_faces_repaired=65,total_sliver_faces_removed=412,
              final_unreal_import_warning_count=0,
              validation='Final FBX independently reimported; native Unreal Final asset saved with zero tangent/binormal warnings. Full shell generator was not rerun after the final 387-face cleanup; its repair function was used on the delivered export.')
(p.WORK/'tangent_repair.json').write_text(json.dumps(repair,indent=2))
print('Selected H28 saved; final tangent import passed.',flush=True)
