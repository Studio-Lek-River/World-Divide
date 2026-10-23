"""Read-only delivery checks, writes audit/contact sheet inside rubble only.

Run with regular Python after two complete generator runs. Run 1 verification
and hashes must be copied to evidence/round4_run1_receipt.json first.
"""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / 'Art/USA/buildings/usa_hq/rubble'


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    verification = json.loads((OUT/'evidence/verification.json').read_text())
    hashes = json.loads((OUT/'evidence/hashes.json').read_text())
    previous = json.loads((OUT/'evidence/round4_run1_receipt.json').read_text())
    checks = {name: digest(OUT/name) == value for name, value in hashes.items()}
    references = {name: digest(REPO/name) == value
                  for name, value in verification['reference_inputs_sha256'].items()}
    baseline=json.loads((OUT/'evidence/round3_baseline/verification.json').read_text())
    assert verification['reference_inputs_sha256']==baseline['reference_inputs_sha256']
    repeated = {name: data['geometry_sha256'] == previous['verification']['source'][name]['geometry_sha256']
                for name, data in verification['source'].items()}
    stable_maps = {name: value == previous['hashes'][name] for name, value in hashes.items()
                   if name.startswith('textures/') or name == 'material_recipe.json'}
    cameras = json.loads((OUT/'review/cameras.json').read_text())
    assert len(cameras) == 14
    assert all(checks.values()) and all(references.values())
    assert all(repeated.values()) and all(stable_maps.values())
    assert verification['generator_sha256'] == digest(REPO/'Tools/Art/usa_hq_rubble.py')
    independent=json.loads((OUT/'evidence/round4_independent_check.json').read_text())
    assert independent['all_checks_passed']
    for name, mesh in independent['meshes'].items():
        assert digest(OUT/(name+'.fbx'))==mesh['sha256']
    for log in ('round4_clean_run_1.log','round4_clean_run_2.log'):
        message=(OUT/'evidence'/log).read_text(encoding='utf-16')
        assert 'CLEAN GENERATOR COMPLETE' in message and 'Traceback' not in message
    sheet = Image.new('RGB', (1280, 4*204), '#252525')
    draw = ImageDraw.Draw(sheet)
    images = {}
    for i, camera in enumerate(cameras):
        path = OUT/'review'/camera['file']
        with Image.open(path) as im:
            assert im.size == (1280, 720)
            images[camera['file']] = {'size': list(im.size), 'sha256': digest(path)}
            thumbnail = im.convert('RGB').resize((320, 180), Image.Resampling.LANCZOS)
        x, y = (i % 4)*320, (i // 4)*204
        sheet.paste(thumbnail, (x, y))
        draw.text((x+6, y+184), camera['file'], fill='white')
    sheet.save(OUT/'review/round4_contact_sheet.png')
    result = {'round': 4, 'asset_receipts': checks, 'reference_inputs_unchanged': references,
              'separate_independent_audit_matches_current_fbx': True,
              'geometry_identical_across_two_clean_runs': repeated,
              'maps_and_recipe_identical_across_two_clean_runs': stable_maps,
              'generator_sha256': verification['generator_sha256'], 'review_images': images,
              'review_scope': 'Blender Cycles, no Unreal operations; producer acceptance pending; independent Round 4 model check complete.',
              'logs': ['round4_clean_run_1.log', 'round4_clean_run_2.log']}
    (OUT/'evidence/final_audit.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({'receipts': len(checks), 'review_images': len(images),
                      'repeat_geometry': repeated, 'all_checks_passed': True}, indent=2))


if __name__ == '__main__':
    main()
