import json, pathlib, subprocess, sys, tempfile
def main():
    tool=pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as td:
        d=pathlib.Path(td)
        cpu={'files':[{'file':'69.json','selected':10,'passed':8,'failed':2,'failures':{'state':2,'cycle_count':1}}]}
        spc={'files':[{'file':'01.json','selected':10,'passed':9,'failed':1,'failures':{'bus_address':1}}]}
        rom={'cases':[{'name':'cputest','passed':False,'error':'catalog-incomplete','test_number':291,'test':'adc edge'}]}
        for n,v in [('cpu.json',cpu),('spc.json',spc),('rom.json',rom)]: (d/n).write_text(json.dumps(v))
        out=d/'q.json'; md=d/'q.md'
        p=subprocess.run([sys.executable,str(tool),'--65816-report',str(d/'cpu.json'),'--spc700-report',str(d/'spc.json'),'--rom-report',str(d/'rom.json'),'--output-json',str(out),'--output-md',str(md)],capture_output=True,text=True)
        assert p.returncode==0,p.stderr
        q=json.loads(out.read_text())
        assert q['defects_total']==3
        assert q['queue'][0]['component']=='65816'
        assert 'cputest' in md.read_text()
    return 0
if __name__=='__main__': raise SystemExit(main())
