import json
import subprocess
from datetime import datetime, timedelta, timezone

REPO = 'G-A-R-D-E-N/GardenTools'
TITLE = 'Weekly real-data run failed'
MAX_AGE = timedelta(days=8)


def marker(run_id, kind, attempt=1):
    return f'<!-- weekly-real-data:{run_id}:{attempt}:{kind} -->'


def already_reported(bodies, run_id, kind, attempt=1):
    return any(marker(run_id, kind, attempt) in (body or '') for body in bodies)


def stamp(run, *keys):
    for key in keys:
        if run.get(key):
            return datetime.fromisoformat(run[key].replace('Z', '+00:00'))
    return None


def problem(run, now):
    if run.get('event') != 'schedule':
        return None
    updated = stamp(run, 'updated_at', 'run_started_at', 'created_at')
    if updated and now - updated > MAX_AGE:
        return None
    if run.get('status') == 'completed':
        return 'failed' if run.get('conclusion') in {
            'failure', 'cancelled', 'timed_out', 'startup_failure', 'action_required'
        } else None
    if run.get('status') in {'queued', 'requested', 'waiting', 'pending'}:
        started = stamp(run, 'run_started_at', 'created_at')
        if started and now - started >= timedelta(hours=2):
            return 'waiting'
    return None


def api(method, path, payload=None):
    args = ['gh', 'api', '--method', method, path]
    if method == 'GET' and 'per_page=' in path:
        args += ['--paginate', '--slurp']
    if payload is not None:
        args += ['--input', '-']
    result = subprocess.run(args, input=json.dumps(payload) if payload is not None else None,
                            text=True, capture_output=True, check=True)
    value = json.loads(result.stdout)
    if method == 'GET' and 'per_page=' in path:
        return [item for page in value for item in page]
    return value


def report_runs(runs, now, request):
    relevant = [(run, problem(run, now)) for run in runs]
    relevant = [(run, kind) for run, kind in relevant if kind]
    if not relevant:
        return
    prefix = f'repos/{REPO}'
    issues = request('GET', f'{prefix}/issues?state=all&per_page=100')
    matching = [item for item in issues if item.get('title') == TITLE and not item.get('pull_request')]
    issue = next((item for item in matching if item.get('state') == 'open'), None)
    bodies = []
    for item in matching:
        bodies.append(item.get('body', ''))
        bodies += [comment.get('body', '') for comment in request('GET', f"{prefix}/issues/{item['number']}/comments?per_page=100")]
    for run, kind in relevant:
        attempt = run.get('run_attempt', 1)
        if already_reported(bodies, run['id'], kind, attempt):
            continue
        detail = ('The scheduled run has waited at least two hours for a runner. Check whether the data machine is offline or busy.'
                  if kind == 'waiting' else f"The scheduled run ended with conclusion: {run.get('conclusion', 'unknown')}.")
        body = f"{detail}\n\nRun: {run['html_url']}\n\n{marker(run['id'], kind, attempt)}"
        if issue:
            request('POST', f"{prefix}/issues/{issue['number']}/comments", {'body': body})
        else:
            issue = request('POST', f'{prefix}/issues', {'title': TITLE, 'body': body})
        bodies.append(body)


def scheduled_runs():
    endpoint = f'repos/{REPO}/actions/workflows/native-real-data.yml/runs?event=schedule&branch=main&per_page=20'
    runs = subprocess.run(['gh', 'api', endpoint, '--paginate', '--slurp'],
                          text=True, capture_output=True, check=True)
    return [run for page in json.loads(runs.stdout) for run in page['workflow_runs']]


def main():
    report_runs(scheduled_runs(), datetime.now(timezone.utc), api)


if __name__ == '__main__':
    main()
