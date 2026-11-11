# Fetches what the pr-life scripts read into the current folder (#3082), with `gh api` REST GETs only, per_page=100,
# and keeps every file it already has: run it again to add only what is new. Usage, from an empty data folder:
#   python3 -I <repo>/knowledge/ci/pr-life/fetch.py 2026-11-02T12:00:00Z 2026-11-11
# The first argument is the oldest PR creation time kept, the second the last day of CI runs fetched.
# About 3 calls per PR and 1 per failed run: 791 PRs took about 3,000 calls (the session's limit is 15,000 an hour).
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from datetime import date, timedelta

REPO = 'repos/smith-ai-lab/smith'


def api(path, jq=None):
    command = ['gh', 'api', f'{REPO}/{path}'] + (['--jq', jq] if jq else [])
    result = subprocess.run(command, capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


def cached(path, fetch):
    if os.path.exists(path) and os.path.getsize(path):
        return
    text = fetch()
    if text is not None:
        with open(path, 'w') as out:
            out.write(text)


def main():
    since, last = sys.argv[1], date.fromisoformat(sys.argv[2])
    for folder in ('cache/tl', 'cache/iss', 'cache/iev', 'cache/files', 'cache/runs2', 'cache/jobs'):
        os.makedirs(folder, exist_ok=True)
    prs = []
    for page in range(1, 30):  # newest first, until older than `since`
        batch = json.loads(api(f'pulls?state=all&sort=created&direction=desc&per_page=100&page={page}') or '[]')
        prs += [p for p in batch if p['created_at'] >= since]
        if len(batch) < 100 or batch[-1]['created_at'] < since:
            break
    json.dump(prs, open('prs.json', 'w'))
    tickets = sorted({int(m.group(1)) for p in prs
                      for m in re.finditer(r'(?i)\b(?:closes|fixes|resolves|part of)\s+#(\d+)', p.get('body') or '')})
    jobs = []
    for p in prs:
        n = p['number']
        # a closed PR's timeline and files do not change; an open one's are fetched again on the next run
        if p['state'] == 'open':
            for stale in (f'cache/tl/{n}.json', f'cache/files/{n}.json'):
                if os.path.exists(stale):
                    os.remove(stale)
        jobs.append((f'cache/tl/{n}.json', f'issues/{n}/timeline?per_page=100', None))
        jobs.append((f'cache/files/{n}.json', f'pulls/{n}/files?per_page=100',
                     '[.[]|{filename,additions,deletions}]'))
    for n in tickets:
        jobs.append((f'cache/iss/{n}.json', f'issues/{n}', '{number,created_at,closed_at,title,labels:[.labels[].name]}'))
        jobs.append((f'cache/iev/{n}.json', f'issues/{n}/events?per_page=100',
                     '[.[]|select(.event=="labeled" or .event=="unlabeled")|{event,label:.label.name,created_at}]'))
    with ThreadPoolExecutor(6) as pool:
        list(pool.map(lambda j: cached(j[0], lambda: api(j[1], j[2])), jobs))
    fields = '.workflow_runs[]|{id,event,head_branch,head_sha,status,conclusion,created_at,updated_at,run_started_at}'
    day = date.fromisoformat(since[:10])
    while day <= last:  # 6-hour windows keep each query under the 1,000 results the runs API returns
        for start in (0, 6, 12, 18):
            path = f'cache/runs2/{day.strftime("%d")}{start:02d}.json'
            if os.path.exists(path) and day < last:
                continue
            lines = []
            for page in range(1, 11):
                window = f'{day}T{start:02d}:00:00Z..{day}T{start + 5:02d}:59:59Z'
                text = api(f'actions/workflows/ci.yml/runs?per_page=100&page={page}&created={window}', fields) or ''
                got = [line for line in text.splitlines() if line.strip()]
                lines += got
                if len(got) < 100:
                    break
            open(path, 'w').write('\n'.join(lines) + '\n')
        day += timedelta(days=1)
    failed = [json.loads(line)['id'] for name in os.listdir('cache/runs2') for line in open(f'cache/runs2/{name}')
              if line.strip() and json.loads(line)['conclusion'] == 'failure'
              and json.loads(line)['event'] not in ('push', 'schedule')]
    jq = '[.jobs[]|select(.conclusion=="failure")|{id,name,started_at,completed_at,steps:[.steps[]|select(.conclusion=="failure")|.name]}]'
    with ThreadPoolExecutor(6) as pool:
        list(pool.map(lambda i: cached(f'cache/jobs/{i}.json', lambda: api(f'actions/runs/{i}/jobs?filter=latest&per_page=100', jq)), failed))
    print(f'{len(prs)} PRs, {len(tickets)} tickets, {len(failed)} failed PR and queue runs')


main()
