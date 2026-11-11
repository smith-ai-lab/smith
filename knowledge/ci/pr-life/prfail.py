# Red PR checks since 11-03: same head later green (flaky/infra) or fixed by a new push; by first failing job (#3082).
import json,glob,os,re,collections as C
runs={}
for f in glob.glob('cache/runs2/*.json'):
    for l in open(f):
        if l.strip(): r=json.loads(l); runs[r['id']]=r
okshas={r['head_sha'] for r in runs.values() if r['conclusion']=='success'}
by=C.defaultdict(C.Counter); dur=C.defaultdict(list)
from datetime import datetime as D
def t(s): return D.fromisoformat(s.replace('Z','+00:00'))
for f in glob.glob('cache/jobs/*.json'):
    i=int(os.path.basename(f)[:-5]); r=runs[i]
    if r['event'] not in('pull_request','workflow_dispatch'): continue
    try: js=json.load(open(f))
    except: js=[]
    if js:
        j=min(js,key=lambda j:j['completed_at'] or 'z'); n=j['name'].split(' (')[0]; n=re.sub(r'test-linux.*','test-linux',n)
        s=(j['steps'] or ['?'])[0]
        if n=='build-linux': n='build-linux (smith or the tests do not build)'
        dur[n].append((t(r['updated_at'])-t(r['run_started_at'] or r['created_at'])).total_seconds()/60)
    else: n='none'
    by[n]['same head later green' if r['head_sha'] in okshas else 'new push']+=1
for n,c in sorted(by.items(),key=lambda x:-sum(x[1].values())):
    d=sorted(dur[n]); md=d[len(d)//2] if d else 0
    print(f'{sum(c.values()):4d} {n}: {dict(c)}  median run min to red {md:.0f}')
