import argparse, json, pathlib, re
from collections import defaultdict
SEVERITY = {
    'state': 100, 'ram': 95, 'cycle_count': 85,
    'cycle_kind': 80, 'bus_kind': 80,
    'cycle_address': 75, 'bus_address': 75,
    'cycle_value': 70, 'bus_value': 70,
}
def load(path):
    return json.loads(path.read_text(encoding='utf-8'))
def opcode_from_file(name):
    stem = pathlib.Path(name).stem
    m = re.match(r'^([0-9A-Fa-f]{2})(?:[._-]([0-9A-Fa-f]{2}))?$', stem)
    return stem.upper() if not m else (m.group(1) + (('-' + m.group(2)) if m.group(2) else '')).upper()
def add_single_step(queue, report, component, source):
    for item in report.get('files', []):
        failed = int(item.get('failed', 0))
        if not failed: continue
        cats = {k:int(v) for k,v in item.get('failures',{}).items() if int(v)}
        dominant = max(cats, key=lambda k:(SEVERITY.get(k,50), cats[k])) if cats else 'unknown'
        key = f'{component}:{opcode_from_file(item.get("file","unknown"))}'
        queue.append({
            'key': key, 'component': component, 'kind': 'single-step',
            'opcode': opcode_from_file(item.get('file','unknown')),
            'source': source, 'selected': int(item.get('selected',0)),
            'failed': failed, 'failure_rate': (failed/max(1,int(item.get('selected',0)))),
            'failure_categories': cats, 'dominant_failure': dominant,
            'priority_score': SEVERITY.get(dominant,50)*100000 + failed,
            'source_hints': ['src/core/cpu.cpp', 'src/core/cpu/'] if component == '65816' else ['src/core/apu.cpp'],
        })
def add_rom(queue, report, source):
    for case in report.get('cases', []):
        if case.get('passed'): continue
        error = case.get('error','unknown')
        number = case.get('test_number')
        test = case.get('test')
        if number is None:
            result = case.get('result') or {}
            number = result.get('diagnostic_test_number')
            test = result.get('diagnostic_test') or test
        score = 9000000 if error in ('runner-failed','status-mismatch','catalog-incomplete') else 6000000
        queue.append({
            'key': f'rom:{case.get("name","unnamed")}:{number if number is not None else error}',
            'component': 'rom-conformance', 'kind': 'rom', 'source': source,
            'name': case.get('name','unnamed'), 'error': error,
            'test_number': number, 'test': test, 'priority_score': score,
            'source_hints': ['src/core/cpu.cpp', 'src/core/apu.cpp', 'src/core/console.cpp', 'src/headless/main.cpp'],
        })
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--65816-report', type=pathlib.Path)
    ap.add_argument('--spc700-report', type=pathlib.Path)
    ap.add_argument('--rom-report', type=pathlib.Path)
    ap.add_argument('--output-json', required=True, type=pathlib.Path)
    ap.add_argument('--output-md', type=pathlib.Path)
    args=ap.parse_args()
    queue=[]; sources=[]
    for path,component in ((args.__dict__['65816_report'],'65816'),(args.spc700_report,'spc700')):
        if path and path.is_file(): add_single_step(queue,load(path),component,str(path)); sources.append(str(path))
    if args.rom_report and args.rom_report.is_file(): add_rom(queue,load(args.rom_report),str(args.rom_report)); sources.append(str(args.rom_report))
    queue.sort(key=lambda x:(-x['priority_score'], -x.get('failure_rate',0), x['key']))
    for i,item in enumerate(queue,1): item['rank']=i
    payload={'format':1,'sources':sources,'defects_total':len(queue),'queue':queue}
    args.output_json.parent.mkdir(parents=True,exist_ok=True)
    args.output_json.write_text(json.dumps(payload,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    if args.output_md:
        lines=['# Accuracy defect queue','',f'Defects: **{len(queue)}**','']
        for item in queue:
            if item['kind']=='single-step':
                cats=', '.join(f'{k}={v}' for k,v in item['failure_categories'].items()) or 'uncategorized'
                lines.append(f"{item['rank']}. **{item['component']} opcode {item['opcode']}** — {item['failed']}/{item['selected']} failed; {cats}")
            else:
                suffix=f" test {item['test_number']:04X}" if isinstance(item.get('test_number'),int) else ''
                desc=f" — {item['test']}" if item.get('test') else ''
                lines.append(f"{item['rank']}. **ROM {item['name']}** — {item['error']}{suffix}{desc}")
        args.output_md.parent.mkdir(parents=True,exist_ok=True)
        args.output_md.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'Accuracy defect queue: {len(queue)} item(s)')
    return 0
if __name__=='__main__': raise SystemExit(main())
