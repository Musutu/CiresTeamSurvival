"""Read-only Bridge UUID -> saved static asset receipt audit. No UE launch/import.

Default prints a receipt report. --write records the report under Art only;
it never publishes EnvironmentPlacements or changes an Unreal asset. The next
integration step must inspect actual bounds/materials/rendering before placement.
"""
import argparse,json,re
from pathlib import Path
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parent.parent
PATTERN=re.compile(r'\[([^\]]+)\].*Starting FBX import: .*?/Tripo3D/([0-9a-f-]{36})/[^\r\n]+? -> (/Game/TripoModels/[^\r\n]+)')
def audit(root):
    request=json.loads((root/'Art/TripoEnvironmentImportRequests.json').read_text(encoding='utf-8-sig'))
    inspection_path=root/'Saved/TripoBridgeInspection.json'
    inspection=json.loads(inspection_path.read_text(encoding='utf-8-sig')) if inspection_path.is_file() else {}
    source={a['uuid']:a for a in request['assets']};seen=[]
    for path in sorted((root/'Saved/Logs').glob('Tripo*.log')):
        for match in PATTERN.finditer(path.read_text(encoding='utf-8-sig',errors='replace').replace('\\','/')):
            stamp,identifier,folder=match.groups()
            if identifier in source:seen.append((stamp,identifier,folder.strip(),str(path.relative_to(root))))
    rows=[]
    for identifier,source_row in source.items():
        candidates=[]
        for stamp,uid,folder,log in sorted(set(seen)):
            if uid!=identifier:continue
            matches=[a for a in inspection.get('assets',[]) if a.get('class') in ('StaticMesh','SkeletalMesh') and a.get('asset','').startswith(folder+'/')]
            dirty=[p for p in inspection.get('unsaved_tripo_packages',[]) if p.startswith(folder+'/')]
            receipt={'folder':folder,'observedAt':stamp,'log':log,'unsavedPackages':dirty,'status':'awaiting_saved_inspection'}
            if len(matches)==1:
                asset=matches[0];name=asset['asset'];package=name.split('.')[0]
                saved=(root/'Content'/(package[len('/Game/'):]+'.uasset')).is_file()
                receipt.update(mesh=name,meshClass=asset['class'],bounds=asset.get('bounds'),materials=asset.get('material_slots'),saved=saved)
                if saved and not dirty:receipt['status']='saved_static_candidate' if asset['class']=='StaticMesh' else 'saved_unexpected_skeletal_review_required'
            elif len(matches)>1:receipt['status']='ambiguous_multiple_meshes_manual_review_required'
            candidates.append(receipt)
        canonical=next((r for r in candidates if r['status']=='saved_static_candidate'),None)
        rows.append({'role':source_row['role'],'uuid':identifier,'status':'saved_import_ready_for_visual_preflight' if canonical else 'not_yet_verified_saved',
                     'canonical':canonical,'receipts':candidates,'duplicatesPreserved':len(candidates)>1})
    return {'schemaVersion':1,'checkedUtc':datetime.now(timezone.utc).isoformat(),'readOnlyUnrealAssets':True,'publishedRuntimePlacements':False,
            'inspectionUtc':inspection.get('created_utc'),'inspectionErrors':inspection.get('errors',[]),'assets':rows}
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    report=audit(ROOT)
    if args.write:(ROOT/'Art/TripoEnvironmentReceipts.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
