"""repos.py — Git repository scanning (T032)

Three-tier config:
  1. explicit  — always-watch paths
  2. scan_roots — directories to search for .git repos up to scan_depth
  3. exclude   — paths to skip

Only repos where branch != default_branch OR is_dirty are returned.
"""

from __future__ import annotations

import logging
from collections.abc import Iterator
from pathlib import Path

from .config import RepoConfig

logger = logging.getLogger(__name__)


def _iter_repos(root: Path, depth: int, exclude: set[str]) -> Iterator[Path]:
    """Yield .git parent directories up to `depth` levels below `root`."""
    if depth < 0:
        return
    try:
        for child in root.iterdir():
            if not child.is_dir():
                continue
            if str(child) in exclude:
                continue
            git_dir = child / ".git"
            if git_dir.exists():
                yield child
            else:
                yield from _iter_repos(child, depth - 1, exclude)
    except PermissionError:
        pass


def _default_branch(repo) -> str:
    """Determine the default branch name for a repo."""
    try:
        # Prefer origin/HEAD
        for ref in repo.references:
            if ref.name in ("origin/HEAD", "refs/remotes/origin/HEAD"):
                return ref.reference.name.split("/")[-1]
    except Exception:
        pass
    # Fall back to main/master
    for name in ("main", "master"):
        if any(h.name == name for h in repo.heads):
            return name
    return "main"


def scan_repos(repo_config: RepoConfig) -> list[dict]:
    """
    Scan repositories and return status for non-clean ones.
    Each entry matches the RepoStatus JSON schema in contracts/redis-schema.md.
    """
    try:
        import git  # noqa: PLC0415 — lazy import
    except ImportError:
        logger.warning("GitPython not available; skipping repo scan")
        return []

    import time  # noqa: PLC0415

    exclude = {str(Path(p).resolve()) for p in repo_config.exclude}
    paths: list[Path] = []

    # Tier 1: explicit paths
    for p in repo_config.explicit:
        paths.append(Path(p).expanduser().resolve())

    # Tier 2: scan roots
    for root_str in repo_config.scan_roots:
        root = Path(root_str).expanduser().resolve()
        if root.is_dir():
            paths.extend(_iter_repos(root, repo_config.scan_depth - 1, exclude))

    results = []
    seen: set[str] = set()
    explicit_paths = {str(Path(p).expanduser().resolve()) for p in repo_config.explicit}

    for path in paths:
        path_str = str(path)
        if path_str in seen or path_str in exclude:
            continue
        seen.add(path_str)
        try:
            repo = git.Repo(path_str)
        except Exception:
            continue

        try:
            branch = repo.active_branch.name
        except TypeError:
            branch = "HEAD"  # detached HEAD

        default = _default_branch(repo)
        is_dirty = repo.is_dirty(untracked_files=True)
        detached = repo.head.is_detached

        if branch == default and not is_dirty:
            continue  # clean — skip (FR-041)

        # Granular working-tree stats
        try:
            wt_diffs = repo.index.diff(None)
        except Exception:
            wt_diffs = []
        changed = sum(1 for d in wt_diffs if d.change_type in ("M", "A", "T"))
        deleted = sum(1 for d in wt_diffs if d.change_type == "D")
        renamed = sum(1 for d in wt_diffs if d.change_type == "R")

        try:
            untracked = len(repo.untracked_files)
        except Exception:
            untracked = 0

        # Ahead/behind upstream
        ahead = 0
        behind = 0
        if not detached:
            try:
                ab = repo.git.rev_list("--left-right", "--count", "HEAD...@{upstream}")
                parts = ab.strip().split()
                if len(parts) == 2:
                    ahead, behind = int(parts[0]), int(parts[1])
            except Exception:
                pass  # no upstream configured

        # Last commit timestamp (branch tip)
        try:
            last_commit_ts = float(repo.head.commit.committed_date)
        except Exception:
            last_commit_ts = 0.0

        results.append(
            {
                "name": path.name,
                "path": path_str,
                "branch": branch,
                "default_branch": default,
                "is_dirty": is_dirty,
                "explicit": path_str in explicit_paths,
                "detached_head": detached,
                "ahead": ahead,
                "behind": behind,
                "untracked_count": untracked,
                "changed_count": changed,
                "deleted_count": deleted,
                "renamed_count": renamed,
                "last_commit_ts": last_commit_ts,
                "ts": time.time(),
            }
        )

    # Explicit repos always appear first, then alphabetically by name
    results.sort(key=lambda r: (0 if r["explicit"] else 1, r["name"]))
    return results
