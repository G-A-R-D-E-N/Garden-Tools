#!/usr/bin/env python3
import argparse
import ctypes
import json
import logging
import os
import re
import shutil
import stat
import subprocess
import sys
import threading
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

REPO = "G-A-R-D-E-N/GardenTools"
REPO_URL = f"https://github.com/{REPO}.git"
PUBLIC_REPO = "G-A-R-D-E-N/Garden-Tools"
SYNC_WORKFLOW = "sync-from-private.yml"
SYNC_EVERY = 300
DEV_BRANCH = "nomad/dev"
CONTEXT = "real-data"
CHECK_RUN = "Native real-data tests (Windows, MSVC, Fallout 4 data)"
WORKFLOW = ".github/workflows/native-real-data.yml"
TOUCHES = ("Radicle/", "PluginEditTool/native/", "cmake/", "CMakeLists.txt", ".github/scripts/",
           ".github/workflows/native-real-data.yml")
EXCLUDED_PREFIXES = ("Radicle/docs/",)
EXCLUDED_SUFFIXES = (".md",)
ROOT = Path(r"C:\ci\real-data")
REPO_DIR = ROOT / "repo"
LOG_DIR = ROOT / "logs"
LOCK = ROOT / "run.lock"
QUEUED = ROOT / "queued.json"
RUNNER_ENV = Path(r"C:\tools\github-runner\fo4-data\.env")
RUNNER_KEYS = ("FO4_DATA_DIR", "CMAKE_BUILD_PARALLEL_LEVEL", "USERPROFILE", "HOME", "APPDATA", "LOCALAPPDATA",
               "TEMP", "TMP")
RUN_SCRIPT = Path(__file__).resolve().with_name("run_real_data.ps1")
TIMEOUT = 3 * 3600
LOCK_MAX_AGE = TIMEOUT + 600
POLL = 60
INTERVAL = 120
RETRY_DELAYS = (5, 15)
READ_ATTEMPTS = 3
KEEP_LOGS = 50
DESC_MAX = 140
BELOW_NORMAL = 0x00004000
PROCESS_QUERY_LIMITED = 0x1000
STILL_ACTIVE = 259
GH = "gh"
SUMMARY = re.compile(r"(\d+) tests? failed out of (\d+)")
FAILED_SECTION = "The following tests FAILED:"
FAILED = re.compile(r"(\d+) - (.+?) \(([A-Za-z][A-Za-z ]*)\)[ \t\r]*$", re.M)


@dataclass(frozen=True)
class Target:
    name: str
    sha: str
    updated: str
    base: str
    number: int | None = None
    parent: str = ""
    draft: bool = False


def run_command(cmd, attempts, cwd=None):
    for number in range(1, attempts + 1):
        try:
            return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                  check=True).stdout
        except (subprocess.CalledProcessError, OSError) as error:
            logging.warning("%s failed (attempt %d of %d): %s", cmd[1], number, attempts, failure_text(error))
            if number == attempts:
                raise
            time.sleep(RETRY_DELAYS[number - 1])


def gh(*args, attempts=READ_ATTEMPTS):
    return run_command([GH, *args], attempts)


def git(*args, cwd=None, attempts=1):
    return run_command(["git", *args], attempts, cwd or REPO_DIR)


def runner_env():
    values = {}
    for line in RUNNER_ENV.read_text(encoding="utf-8-sig").splitlines():
        key, sep, value = line.partition("=")
        if sep and key in RUNNER_KEYS:
            values[key] = value
    if "FO4_DATA_DIR" not in values:
        raise SystemExit("FO4_DATA_DIR is not set in the data runner environment")
    return values


def open_targets():
    prs = json.loads(gh("pr", "list", "--repo", REPO, "--state", "open", "--limit", "200",
                        "--json", "number,headRefOid,baseRefName,updatedAt,isDraft"))
    targets = [Target(f"#{pr['number']}", pr["headRefOid"], pr["updatedAt"], pr["baseRefName"], pr["number"],
                      draft=pr["isDraft"]) for pr in prs]
    head = gh("api", f"repos/{REPO}/git/ref/heads/{DEV_BRANCH}", "--jq", ".object.sha").strip()
    commit = json.loads(gh("api", f"repos/{REPO}/commits/{head}"))
    targets.append(Target(DEV_BRANCH, head, commit["commit"]["committer"]["date"], "", None,
                          commit["parents"][0]["sha"]))
    return targets


def order(targets):
    def rank(t):
        if t.number is None:
            return 0
        return 1 if t.base == DEV_BRANCH and not t.draft else 2

    newest_first = sorted(targets, key=lambda t: t.updated, reverse=True)
    return sorted(newest_first, key=rank)


def current_head(t):
    if t.number:
        return gh("api", f"repos/{REPO}/pulls/{t.number}", "--jq", ".head.sha").strip()
    return gh("api", f"repos/{REPO}/git/ref/heads/{DEV_BRANCH}", "--jq", ".object.sha").strip()


def head_moved(t):
    try:
        return current_head(t) != t.sha
    except (subprocess.CalledProcessError, OSError):
        return False


def changed_paths(t):
    if t.number:
        return gh("api", f"repos/{REPO}/pulls/{t.number}/files", "--paginate", "--jq", ".[].filename").splitlines()
    return gh("api", f"repos/{REPO}/compare/{t.parent}...{t.sha}", "--jq", ".files[].filename").splitlines()


def latest_status(sha):
    lines = gh("api", f"repos/{REPO}/commits/{sha}/statuses", "--paginate", "--jq",
               f'.[] | select(.context == "{CONTEXT}") | .state + " " + .description').splitlines()
    state, _, description = lines[0].partition(" ") if lines else (None, "", "")
    return state, description


def passed_earlier(sha):
    conclusions = gh("api", f"repos/{REPO}/commits/{sha}/check-runs", "--paginate", "--jq",
                     f'.check_runs[] | select(.name == "{CHECK_RUN}") | .conclusion').splitlines()
    return "success" in conclusions


def in_scope(path):
    if path.endswith(EXCLUDED_SUFFIXES) or path.startswith(EXCLUDED_PREFIXES):
        return False
    return any(path == item or (item.endswith("/") and path.startswith(item)) for item in TOUCHES)


def touches(paths):
    return any(in_scope(path) for path in paths)


def plan(t):
    if t.draft:
        return "draft"
    state, description = latest_status(t.sha)
    if state not in (None, "pending") and (state, description) != ("error", "superseded"):
        return "done"
    if passed_earlier(t.sha):
        return "earlier"
    return "run" if touches(changed_paths(t)) else "not affected"


def clean(text):
    user = os.environ.get("USERNAME", "")
    if user:
        text = re.sub(re.escape(user), "", text, flags=re.IGNORECASE)
    text = re.sub(r"\S*[\\/]\S*", "", text)
    return re.sub(r"[^A-Za-z0-9_.,()#: -]", "", text)[:DESC_MAX]


def failure_text(error):
    return clean(" ".join((getattr(error, "stderr", None) or str(error)).split()))


def post(sha, state, description):
    gh("api", "--method", "POST", f"repos/{REPO}/statuses/{sha}", "-f", f"state={state}",
       "-f", f"context={CONTEXT}", "-f", f"description={clean(description)}", attempts=2)


def load_queued():
    try:
        return set(json.loads(QUEUED.read_text(encoding="utf-8")))
    except (FileNotFoundError, ValueError):
        return set()


def save_queued(queued):
    QUEUED.write_text(json.dumps(sorted(queued)), encoding="utf-8")


def post_queue(targets, queued):
    for position, t in enumerate(targets, 1):
        text = f"queued (position {position} of {len(targets)})"
        queued.add(t.sha)
        try:
            if latest_status(t.sha) != ("pending", text):
                post(t.sha, "pending", text)
        except Exception:
            logging.exception("%s: queued status not posted", t.name)
    save_queued(queued)


def clear_stale_queue(live, queued):
    for sha in sorted(queued - live):
        try:
            state, description = latest_status(sha)
            if state == "pending" and description.startswith("queued"):
                post(sha, "error", "superseded")
            queued.discard(sha)
        except Exception:
            logging.exception("%s: queued status not cleared", sha[:12])
    save_queued(queued)


def failed_names(text):
    section = text.partition(FAILED_SECTION)[2]
    return [name for _, name, _ in FAILED.findall(section)]


def outcome(text, rc):
    found = SUMMARY.findall(text)
    if not found:
        return "failure", f"no test summary (exit {rc}), 0 tests run"
    failed, total = (int(n) for n in found[-1])
    if rc == 0 and failed == 0:
        return "success", f"{total} tests, 0 failed"
    names = failed_names(text)
    detail = f": {names[0]}" if failed and names else f" (exit {rc})"
    return "failure", f"{total} tests, {failed} failed{detail}"


def remove_tree(path):
    def retry_writable(func, target, _exc):
        os.chmod(target, stat.S_IWRITE)
        func(target)

    if path.exists():
        shutil.rmtree(path, onexc=retry_writable)


def prepare_checkout(t):
    if not (REPO_DIR / ".git").exists():
        ROOT.mkdir(parents=True, exist_ok=True)
        git("clone", "--no-checkout", REPO_URL, str(REPO_DIR), cwd=ROOT)
    ref = f"refs/pull/{t.number}/merge" if t.number else f"refs/heads/{DEV_BRANCH}"
    git("fetch", "--no-tags", "origin", f"+{ref}:refs/real-data/candidate", attempts=READ_ATTEMPTS)
    commit, *parents = git("rev-list", "--parents", "-n", "1", "refs/real-data/candidate").split()
    current = parents[1:] == [t.sha] if t.number else commit == t.sha
    if not current:
        return False
    git("clean", "-ffdx", "-e", "/build/")
    remove_tree(REPO_DIR / "build" / "zlib-src")
    git("checkout", "-f", "--detach", "refs/real-data/candidate")
    return True


def workflow_file():
    path = REPO_DIR / WORKFLOW
    if path.exists():
        return path
    git("fetch", "--no-tags", "origin", f"+refs/heads/{DEV_BRANCH}:refs/real-data/dev", attempts=READ_ATTEMPTS)
    fallback = ROOT / "fallback" / Path(WORKFLOW).name
    fallback.parent.mkdir(parents=True, exist_ok=True)
    fallback.write_text(git("show", f"refs/real-data/dev:{WORKFLOW}"), encoding="utf-8")
    return fallback


def start_job(t, log_path, workflow):
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, **runner_env(), GITHUB_EVENT_NAME="pull_request" if t.number else "push",
               GITHUB_BASE_REF=t.base)
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        return subprocess.Popen(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                 str(RUN_SCRIPT), "-Workflow", str(workflow)], cwd=REPO_DIR, env=env,
                                stdout=log, stderr=subprocess.STDOUT, creationflags=BELOW_NORMAL)


def kill_tree(proc):
    subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True)
    proc.wait()


def wait_job(t, proc):
    deadline = time.monotonic() + TIMEOUT
    while True:
        try:
            return proc.wait(timeout=POLL)
        except subprocess.TimeoutExpired:
            if head_moved(t):
                kill_tree(proc)
                post(t.sha, "error", "superseded")
                return None
            if time.monotonic() > deadline:
                kill_tree(proc)
                post(t.sha, "error", "timed out")
                return None


def run_target(t):
    if not prepare_checkout(t):
        logging.info("%s %s: merge ref not current; left due", t.name, t.sha[:12])
        return
    workflow = workflow_file()
    post(t.sha, "pending", "running on the local data runner")
    log_path = LOG_DIR / f"{t.sha}.log"
    logging.info("%s %s: running", t.name, t.sha[:12])
    proc = start_job(t, log_path, workflow)
    try:
        rc = wait_job(t, proc)
    finally:
        if proc.poll() is None:
            kill_tree(proc)
    prune_logs()
    if rc is None:
        return
    if current_head(t) != t.sha:
        post(t.sha, "error", "superseded")
        return
    state, text = outcome(log_path.read_text(encoding="utf-8", errors="replace"), rc)
    logging.info("%s %s: %s", t.name, t.sha[:12], text)
    post(t.sha, state, text)


def pid_alive(pid):
    kernel32 = ctypes.windll.kernel32
    kernel32.OpenProcess.restype = ctypes.c_void_p
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED, False, pid)
    if not handle:
        return False
    code = ctypes.c_ulong()
    kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel32.CloseHandle(handle)
    return code.value == STILL_ACTIVE


def lock_is_stale(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return time.time() - float(data["started"]) > LOCK_MAX_AGE or not pid_alive(int(data["pid"]))
    except FileNotFoundError:
        return True
    except (ValueError, KeyError, TypeError):
        return time.time() - path.stat().st_mtime > 60


def acquire_lock():
    ROOT.mkdir(parents=True, exist_ok=True)
    for _ in range(2):
        try:
            with LOCK.open("x", encoding="utf-8") as lock:
                lock.write(json.dumps({"pid": os.getpid(), "started": time.time()}))
            return True
        except FileExistsError:
            if not lock_is_stale(LOCK):
                return False
            LOCK.unlink(missing_ok=True)
    return False


def release_lock():
    LOCK.unlink(missing_ok=True)


def prune_logs():
    logs = sorted(LOG_DIR.glob("*.log"), key=lambda path: path.stat().st_mtime, reverse=True)
    for old in logs[KEEP_LOGS:]:
        old.unlink(missing_ok=True)


def run_pass(dry_run=False):
    if not dry_run and not acquire_lock():
        logging.info("a run holds the lock; pass skipped")
        return Counter()
    counts = Counter()
    try:
        due = []
        queued = load_queued()
        targets = order(open_targets())
        for t in targets:
            try:
                action = plan(t)
                counts[action] += 1
                if dry_run:
                    print(f"{t.name:<12} {t.sha[:12]}  {action}")
                elif action == "earlier":
                    logging.info("%s %s: passed on the earlier runner", t.name, t.sha[:12])
                    post(t.sha, "success", "passed on the earlier runner")
                elif action == "not affected":
                    logging.info("%s %s: not affected", t.name, t.sha[:12])
                    post(t.sha, "success", "not affected")
                elif action == "run":
                    due.append(t)
            except Exception:
                logging.exception("%s: failed for this target", t.name)
        if not dry_run:
            clear_stale_queue({t.sha for t in targets if not t.draft}, queued)
            post_queue(due, queued)
        for index, t in enumerate(due):
            try:
                run_target(t)
            except Exception:
                logging.exception("%s: failed for this target", t.name)
            post_queue(due[index + 1:], queued)
    finally:
        if not dry_run:
            release_lock()
    if dry_run:
        print(dict(counts))
    return counts


def request_public_sync():
    try:
        gh("workflow", "run", SYNC_WORKFLOW, "--repo", PUBLIC_REPO, "--ref", "main", attempts=1)
    except (subprocess.CalledProcessError, OSError) as error:
        logging.warning("public CI sync not started: %s", failure_text(error))


def public_sync_loop():
    while True:
        request_public_sync()
        time.sleep(SYNC_EVERY)


def main(argv=None):
    global GH
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--gh", default="gh")
    args = parser.parse_args(argv)
    GH = args.gh
    if args.dry_run:
        run_pass(dry_run=True)
        return 0
    ROOT.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s",
                        handlers=[logging.FileHandler(ROOT / "service.log", encoding="utf-8"),
                                  logging.StreamHandler()])
    runner_env()
    if args.once:
        run_pass()
        return 0
    threading.Thread(target=public_sync_loop, daemon=True).start()
    while True:
        try:
            run_pass()
        except Exception:
            logging.exception("pass failed")
        time.sleep(INTERVAL)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except BaseException:
        logging.exception("service stopped")
        raise
