# Failed queue runs since 11-03: did the PR merge later with no new commit (not its fault)? (#3082)
import json,glob,os,re,collections as C
from datetime import datetime as D
def t(s): return D.fromisoformat(s.replace('Z','+00:00'))
runs={}
for f in glob.glob('cache/runs2/*.json'):
    for l in open(f):
        if l.strip(): r=json.loads(l); runs[r['id']]=r
prs={p['number']:p for p in json.load(open('prs.json'))}
res=C.Counter(); byjob=C.defaultdict(C.Counter)
for f in glob.glob('cache/jobs/*.json'):
    r=runs[int(os.path.basename(f)[:-5])]
    if r['event']!='merge_group': continue
    n=int(re.search(r'pr-(\d+)-',r['head_branch']).group(1)); p=prs.get(n)
    js=json.load(open(f)) if os.path.getsize(f) else []
    j=re.sub(r'test-linux.*','test-linux',min(js,key=lambda j:j['completed_at'] or 'z')['name'].split(' (')[0]) if js else 'none'
    if not p: k='not cached'
    elif not p['merged_at']: k='never merged'
    elif any(e['event']=='committed' and t(e['committer']['date'])>t(r['created_at']) for e in json.load(open(f'cache/tl/{n}.json'))): k='pushed again'
    else: k='merged unchanged'
    res[k]+=1; byjob[j][k]+=1
print(dict(res))
for j,c in sorted(byjob.items(),key=lambda x:-sum(x[1].values())): print(sum(c.values()),j,dict(c))
