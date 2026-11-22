import json, pathlib, subprocess, sys, tempfile
def report_files_65816(sample=0, break_one=False):
    files=[]; total=0
    for op in range(256):
        for mode in ('e','n'):
            parsed=3; selected=parsed if sample==0 else min(sample,parsed)
            files.append({'file':f'{op:02x}.{mode}.json','parsed':parsed,'selected':selected,'passed':selected,'failed':0,'failures':{}}); total+=selected
    if break_one: files.pop()
    return {'format':1,'sample_per_file':sample,'files_total':len(files),'cases':total,'passed':total,'failed':0,'files':files}
def report_files_spc(sample=0):
    files=[]; total=0
    for op in range(256):
        parsed=3; selected=parsed if sample==0 else min(sample,parsed)
        files.append({'file':f'{op:02x}.json','parsed':parsed,'selected':selected,'passed':selected,'failed':0,'failures':{}}); total+=selected
    return {'format':1,'sample_per_file':sample,'files_total':len(files),'cases':total,'passed':total,'failed':0,'files':files}
def main():
    gate=pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as td:
        d=pathlib.Path(td)
        paths={
            'cpu':d/'cpu.json','spc':d/'spc.json','hw':d/'hw.json','out':d/'out.json'
        }
        paths['cpu'].write_text(json.dumps(report_files_65816()))
        paths['spc'].write_text(json.dumps(report_files_spc()))
        paths['hw'].write_text(json.dumps({'format':2,'executed':2,'passed':2,'failed':0,'skipped':0,'cases':[
            {'name':'gilyon 65C816 full conformance','passed':True},
            {'name':'gilyon SPC700 conformance','passed':True},
        ]}))
        cmd=[sys.executable,str(gate),'--65816-report',str(paths['cpu']),'--spc700-report',str(paths['spc']),'--hardware-report',str(paths['hw']),'--output-json',str(paths['out'])]
        p=subprocess.run(cmd,capture_output=True,text=True)
        assert p.returncode==0, p.stderr+p.stdout
        assert json.loads(paths['out'].read_text())['passed'] is True
        paths['cpu'].write_text(json.dumps(report_files_65816(sample=1)))
        p=subprocess.run(cmd,capture_output=True,text=True)
        assert p.returncode!=0
        paths['cpu'].write_text(json.dumps(report_files_65816(break_one=True)))
        p=subprocess.run(cmd,capture_output=True,text=True)
        assert p.returncode!=0
    print('final_conformance_gate_smoke: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
