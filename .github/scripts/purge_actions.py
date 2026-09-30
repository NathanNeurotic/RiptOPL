#!/usr/bin/env python3
"""
Purge old GitHub Actions workflow runs and their associated artifacts/logs.

Usage:
    python .github/scripts/purge_actions.py --repo NathanNeurotic/Open-PS2-Loader --days 14
    python .github/scripts/purge_actions.py --repo NathanNeurotic/POPSLoader --days 14
    python .github/scripts/purge_actions.py --repo NathanNeurotic/PS2-Servers --days 14
"""

import os
import sys
import time
import argparse
import subprocess
import requests
from datetime import datetime, timezone, timedelta
from concurrent.futures import ThreadPoolExecutor, as_completed

def get_auth_token():
    # Check environment variable first
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        return token
    # Fallback to gh CLI
    try:
        return subprocess.check_output(['gh', 'auth', 'token'], stderr=subprocess.DEVNULL).decode().strip()
    except Exception as e:
        print(f"Error obtaining token via 'gh auth token': {e}")
        print("Please authenticate with 'gh auth login' or set GITHUB_TOKEN environment variable.")
        sys.exit(1)

def check_rate_limit(session):
    try:
        resp = session.get('https://api.github.com/rate_limit', timeout=10)
        if resp.status_code == 200:
            core = resp.json()['resources']['core']
            reset_time = datetime.fromtimestamp(core['reset'], tz=timezone.utc).strftime('%H:%M:%S UTC')
            return core['remaining'], core['limit'], reset_time
    except Exception:
        pass
    return None, None, None

def delete_single_run(session, repo, run_id):
    url = f"https://api.github.com/repos/{repo}/actions/runs/{run_id}"
    for attempt in range(4):
        try:
            resp = session.delete(url, timeout=15)
            if resp.status_code in (204, 404):
                return True, run_id, resp.status_code
            elif resp.status_code in (403, 429):
                text = resp.text.lower()
                retry_after = int(resp.headers.get('Retry-After', 30))
                if 'secondary rate limit' in text or 'abuse' in text:
                    time.sleep(retry_after)
                    continue
                # Primary rate limit
                return False, run_id, 403
            elif resp.status_code >= 500:
                time.sleep(1 + attempt)
                continue
            return False, run_id, resp.status_code
        except requests.RequestException:
            time.sleep(1 + attempt)
    return False, run_id, -1

def purge_runs(session, repo, days_old=14, max_workers=10, min_reserve=50, max_deletions=None):
    cutoff_dt = datetime.now(timezone.utc) - timedelta(days=days_old)
    cutoff_date = cutoff_dt.strftime('%Y-%m-%d')
    print("=" * 72)
    print(f"Target Repository : {repo}")
    print(f"Cutoff Date       : < {cutoff_date} ({days_old} days retention)")
    print(f"Concurrency       : {max_workers} worker threads")
    print(f"Safety Reserve    : {min_reserve} requests")
    print("=" * 72)

    remaining, limit, reset_time = check_rate_limit(session)
    if remaining is not None:
        print(f"API Rate Limit: {remaining}/{limit} remaining (resets at {reset_time})")
        if remaining <= min_reserve:
            print(f"[!] Remaining quota is too low ({remaining} <= {min_reserve}). Try after {reset_time}.")
            return 0

    total_deleted = 0
    start_time = time.time()
    batch_idx = 0

    while True:
        if max_deletions and total_deleted >= max_deletions:
            print(f"Reached target deletion ceiling of {max_deletions} runs.")
            break

        remaining, limit, reset_time = check_rate_limit(session)
        if remaining is not None and remaining <= min_reserve:
            print(f"\n[!] Rate limit safety threshold reached ({remaining}/{limit} remaining).")
            print(f"    Stopping to prevent token lockout. Rate limit resets at {reset_time}.")
            break

        url = f"https://api.github.com/repos/{repo}/actions/runs"
        params = {
            'created': f"<{cutoff_date}",
            'per_page': 100
        }
        
        try:
            resp = session.get(url, params=params, timeout=15)
        except requests.RequestException as e:
            print(f"Network error querying runs: {e}, retrying in 3s...")
            time.sleep(3)
            continue

        if resp.status_code == 403:
            print(f"\n[!] Received HTTP 403. Primary rate limit reached or secondary cooldown.")
            break
        elif resp.status_code != 200:
            print(f"Unexpected response fetching runs: HTTP {resp.status_code} - {resp.text}")
            break

        data = resp.json()
        runs = data.get('workflow_runs', [])
        total_remaining_reported = data.get('total_count', 0)

        if not runs:
            print(f"\n[OK] No runs found matching criteria (< {cutoff_date}).")
            break

        batch_idx += 1
        batch_deleted = 0
        batch_failed = 0

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = {executor.submit(delete_single_run, session, repo, r['id']): r for r in runs}
            for future in as_completed(futures):
                success, run_id, status = future.result()
                if success:
                    batch_deleted += 1
                else:
                    batch_failed += 1

        total_deleted += batch_deleted
        elapsed = time.time() - start_time
        speed = total_deleted / elapsed if elapsed > 0 else 0
        print(f"Batch {batch_idx:3d}: Deleted {batch_deleted:3d} runs | "
              f"Cumulative: {total_deleted:5d} | "
              f"Approx Remaining: {total_remaining_reported:5d} | "
              f"Speed: {speed:4.1f}/s")

        if batch_deleted == 0 and batch_failed > 0:
            print("All deletions in current batch failed (rate limit or permission). Stopping.")
            break

    elapsed = time.time() - start_time
    print("-" * 72)
    print(f"Finished {repo}: Successfully deleted {total_deleted} runs & artifacts in {elapsed:.1f}s.")
    return total_deleted

def main():
    parser = argparse.ArgumentParser(description="Purge GitHub Actions runs older than N days.")
    parser.add_argument('--repo', default='NathanNeurotic/Open-PS2-Loader', help="GitHub repo (owner/repo)")
    parser.add_argument('--days', type=int, default=14, help="Retention period in days (default: 14)")
    parser.add_argument('--workers', type=int, default=10, help="Concurrency workers (default: 10)")
    parser.add_argument('--safety-margin', type=int, default=50, help="Min API calls reserve (default: 50)")
    parser.add_argument('--limit', type=int, default=None, help="Max runs to delete in this session")

    args = parser.parse_args()

    token = get_auth_token()
    session = requests.Session()
    session.headers.update({
        'Authorization': f'Bearer {token}',
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'ActionsCleanup/1.0'
    })

    purge_runs(
        session=session,
        repo=args.repo,
        days_old=args.days,
        max_workers=args.workers,
        min_reserve=args.safety_margin,
        max_deletions=args.limit
    )

if __name__ == '__main__':
    main()
