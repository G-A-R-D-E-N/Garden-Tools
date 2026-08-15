#!/usr/bin/env python3
"""On-demand supervisor for the self-hosted GitHub Actions runners.

Deployed to /usr/local/lib/gh-runner/ghrunnerd.py on the runner host. Keeps at
most MAX_CONCURRENT runner listeners active so two heavy builds never overlap,
wakes runners from GitHub webhooks, and stops them again once idle.

Design rationale, deployment steps and operating commands are in
docs/internal/CI_RUNNERS.md. This file carries no inline commentary by repo
source policy (tools/check-source-policy.py).

Subcommands: serve | drain | idle-stop | sweep | status | wake <unit>
"""

import fcntl
import hashlib
import hmac
import json
import os
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

STATE = "/var/lib/gh-runner"
PENDING = os.path.join(STATE, "pending")
LOCK = os.path.join(STATE, "lock")
SECRET_FILE = "/etc/gh-runner/webhook.secret"

MAX_CONCURRENT = 1
IDLE_MINUTES = 5
IDLE_MINUTES_CONTENDED = 2
LISTEN = ("127.0.0.1", 9494)

P = "actions.runner."
UNITS = {
    "repo:PRISMA-USER-INTERFACE-FRAMEWORK/PluginEditTool":
        P + "PRISMA-USER-INTERFACE-FRAMEWORK-PluginEditTool.debian-wine-msvc-plugin-edit-tool-2.service",
    "org:PRISMA-USER-INTERFACE-FRAMEWORK":
        P + "PRISMA-USER-INTERFACE-FRAMEWORK.debian-wine-msvc.service",
    "org:Fallout-Anomaly":
        P + "Fallout-Anomaly.debian-wine-msvc.service",
    "org:G-A-R-D-E-N":
        P + "G-A-R-D-E-N.debian-wine-msvc.service",
    "repo:G-A-R-D-E-N/CO-BETA":
        P + "G-A-R-D-E-N-CO-BETA.debian-wine-msvc-2.service",
    "repo:G-A-R-D-E-N/FO4IDE":
        P + "G-A-R-D-E-N-FO4-IDE.debian-wine-msvc-fo4-ide-2.service",
    "repo:G-A-R-D-E-N/FO4-IDE":
        P + "G-A-R-D-E-N-FO4-IDE.debian-wine-msvc-fo4-ide-2.service",
}
ALL_UNITS = sorted(set(UNITS.values()))


def log(msg):
    print("[gh-runner] %s" % msg, flush=True)


def sh(*args):
    return subprocess.run(args, capture_output=True, text=True)


def is_active(unit):
    return sh("systemctl", "is-active", "--quiet", unit).returncode == 0


def active_units():
    return [u for u in ALL_UNITS if is_active(u)]


def job_running():
    """True while any runner is executing a job.

    Runner.Worker exists for exactly the lifetime of a job. The bracket in the
    pattern stops pgrep from matching its own command line.
    """
    return sh("pgrep", "-f", "[R]unner\\.Worker").returncode == 0


def resolve(repo_full_name, org_login):
    """Map a webhook payload to a runner unit, preferring a repo-specific one."""
    if repo_full_name and "repo:" + repo_full_name in UNITS:
        return UNITS["repo:" + repo_full_name]
    if org_login and "org:" + org_login in UNITS:
        return UNITS["org:" + org_login]
    if repo_full_name and "/" in repo_full_name:
        owner = repo_full_name.split("/", 1)[0]
        return UNITS.get("org:" + owner)
    return None


def mark_pending(unit):
    os.makedirs(PENDING, exist_ok=True)
    open(os.path.join(PENDING, unit), "w").close()


def pending_units():
    if not os.path.isdir(PENDING):
        return []
    return sorted(n for n in os.listdir(PENDING) if n in ALL_UNITS)


def clear_pending(unit):
    try:
        os.unlink(os.path.join(PENDING, unit))
    except FileNotFoundError:
        pass


def with_lock(fn):
    os.makedirs(STATE, exist_ok=True)
    with open(LOCK, "w") as fh:
        fcntl.flock(fh, fcntl.LOCK_EX)
        try:
            return fn()
        finally:
            fcntl.flock(fh, fcntl.LOCK_UN)


def _drain():
    """Start pending runners up to MAX_CONCURRENT, never pre-empting a live job."""
    started = []
    for unit in pending_units():
        active = active_units()
        if unit in active:
            clear_pending(unit)
            continue
        if len(active) >= MAX_CONCURRENT:
            log("slot full (%s active), %s stays queued" % (len(active), unit))
            break
        r = sh("systemctl", "start", unit)
        if r.returncode == 0:
            clear_pending(unit)
            started.append(unit)
            log("started %s" % unit)
        else:
            log("FAILED to start %s: %s" % (unit, r.stderr.strip()))
    return started


def cmd_wake(unit):
    mark_pending(unit)
    with_lock(_drain)


def cmd_drain():
    with_lock(_drain)


def _idle_path(unit):
    return os.path.join(STATE, "idle-since." + unit)


def cmd_idle_stop():
    """Stop runners idle past the threshold, then let the next one in.

    The threshold drops to IDLE_MINUTES_CONTENDED when another runner is waiting
    for the slot, so a queued repo is not stuck behind the full idle timer. A
    runner must read idle across two consecutive ticks before it can be stopped,
    which keeps a just-started runner from being stopped before it claims its job.
    """
    if job_running():
        for u in ALL_UNITS:
            try:
                os.unlink(_idle_path(u))
            except FileNotFoundError:
                pass
        return
    now = time.time()
    contended = bool(pending_units())
    threshold = (IDLE_MINUTES_CONTENDED if contended else IDLE_MINUTES) * 60
    for unit in active_units():
        p = _idle_path(unit)
        if not os.path.exists(p):
            open(p, "w").close()
            continue
        if now - os.path.getmtime(p) >= threshold:
            def stop():
                """Re-check under the lock so a runner that just took a job survives."""
                if job_running():
                    return
                sh("systemctl", "stop", unit)
                log("stopped idle %s" % unit)
            with_lock(stop)
            try:
                os.unlink(p)
            except FileNotFoundError:
                pass
    with_lock(_drain)


def cmd_sweep():
    """Fallback wake giving every runner a turn, in case a webhook was missed."""
    for unit in ALL_UNITS:
        mark_pending(unit)
    with_lock(_drain)


def cmd_status():
    print("job running : %s" % job_running())
    print("max concurrent: %s   idle stop: %s min (%s min contended)"
          % (MAX_CONCURRENT, IDLE_MINUTES, IDLE_MINUTES_CONTENDED))
    act = active_units()
    pend = pending_units()
    for u in ALL_UNITS:
        flags = []
        if u in act:
            flags.append("ACTIVE")
        if u in pend:
            flags.append("pending")
        print("  %-104s %s" % (u, ",".join(flags) or "stopped"))


def load_secret():
    with open(SECRET_FILE, "rb") as fh:
        return fh.read().strip()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _reply(self, code, msg):
        body = (msg + "\n").encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            return self._reply(400, "bad length")
        if n <= 0 or n > 2_000_000:
            return self._reply(400, "bad length")
        raw = self.rfile.read(n)

        sig = self.headers.get("X-Hub-Signature-256", "")
        expect = "sha256=" + hmac.new(load_secret(), raw, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(sig, expect):
            log("rejected: bad signature")
            return self._reply(401, "bad signature")

        event = self.headers.get("X-GitHub-Event", "")
        if event == "ping":
            return self._reply(200, "pong")
        if event != "workflow_job":
            return self._reply(200, "ignored")

        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            return self._reply(400, "bad json")
        if payload.get("action") != "queued":
            return self._reply(200, "ignored")

        repo = (payload.get("repository") or {}).get("full_name")
        org = (payload.get("organization") or {}).get("login")
        unit = resolve(repo, org)
        if not unit:
            log("no runner mapped for repo=%s org=%s" % (repo, org))
            return self._reply(200, "unmapped")

        log("queued job for %s -> waking %s" % (repo, unit))
        cmd_wake(unit)
        return self._reply(202, "waking")


def cmd_serve():
    load_secret()
    srv = HTTPServer(LISTEN, Handler)
    log("listening on %s:%s" % LISTEN)
    srv.serve_forever()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    if cmd == "serve":
        cmd_serve()
    elif cmd == "drain":
        cmd_drain()
    elif cmd == "idle-stop":
        cmd_idle_stop()
    elif cmd == "sweep":
        cmd_sweep()
    elif cmd == "status":
        cmd_status()
    elif cmd == "wake" and len(sys.argv) > 2:
        cmd_wake(sys.argv[2])
    else:
        print(__doc__)
        print("usage: ghrunnerd.py {serve|drain|idle-stop|sweep|status|wake <unit>}")
        sys.exit(2)


if __name__ == "__main__":
    main()
