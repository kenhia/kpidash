"""tests/test_repos.py — T034

Unit tests for scan_repos() using mocked GitPython.
"""

from __future__ import annotations

from unittest.mock import MagicMock, patch

from kpidash_client.config import RepoConfig

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _make_repo(
    branch: str = "feature/x",
    is_dirty: bool = True,
    default: str = "main",
    detached: bool = False,
    ahead_behind: str = "0\t0",
    untracked: list | None = None,
    wt_diffs: list | None = None,
    committed_date: int = 1_700_000_000,
):
    """Build a mock GitPython Repo with the given branch/dirty state."""
    repo = MagicMock()
    head = MagicMock()
    head.name = branch
    repo.active_branch = head
    repo.is_dirty.return_value = is_dirty
    repo.head.is_detached = detached

    # last commit ts
    repo.head.commit.committed_date = committed_date

    ref = MagicMock()
    ref.name = "origin/HEAD"
    ref.reference = MagicMock()
    ref.reference.name = f"origin/{default}"
    repo.references = [ref]

    main_head = MagicMock()
    main_head.name = default
    repo.heads = [main_head]

    # ahead/behind
    repo.git.rev_list.return_value = ahead_behind

    # untracked files
    repo.untracked_files = untracked if untracked is not None else []

    # working tree diffs
    if wt_diffs is not None:
        repo.index.diff.return_value = wt_diffs
    else:
        repo.index.diff.return_value = []

    return repo


def _make_diff(change_type: str = "M"):
    d = MagicMock()
    d.change_type = change_type
    return d


# ---------------------------------------------------------------------------
# Explicit paths
# ---------------------------------------------------------------------------


def test_explicit_path_dirty_included(tmp_path):
    repo_path = tmp_path / "myrepo"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    mock_repo = _make_repo(branch="feature/abc", is_dirty=True)

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    assert results[0]["name"] == "myrepo"
    assert results[0]["is_dirty"] is True


def test_clean_repo_on_default_branch_excluded(tmp_path):
    repo_path = tmp_path / "clean"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    mock_repo = _make_repo(branch="main", is_dirty=False)

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert results == []


def test_non_default_branch_clean_included(tmp_path):
    """A clean repo on a non-default branch should still appear."""
    repo_path = tmp_path / "wip"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    mock_repo = _make_repo(branch="feature/new", is_dirty=False)

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    assert results[0]["branch"] == "feature/new"


# ---------------------------------------------------------------------------
# scan_roots depth
# ---------------------------------------------------------------------------


def test_scan_roots_finds_direct_child(tmp_path):
    repo_path = tmp_path / "myrepo"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(scan_roots=[str(tmp_path)], scan_depth=2)
    mock_repo = _make_repo(branch="dev", is_dirty=True)

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    assert results[0]["name"] == "myrepo"


def test_scan_roots_depth_limit(tmp_path):
    """Repos deeper than scan_depth should not be found."""
    level1 = tmp_path / "a"
    level2 = level1 / "b"
    level3 = level2 / "c"
    level3.mkdir(parents=True)
    (level3 / ".git").mkdir()

    # scan_depth=2 means we look at tmp_path/a and tmp_path/a/b — not a/b/c
    cfg = RepoConfig(scan_roots=[str(tmp_path)], scan_depth=2)
    mock_repo = _make_repo(branch="dev")

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert results == []


# ---------------------------------------------------------------------------
# Exclude paths
# ---------------------------------------------------------------------------


def test_exclude_path_skipped(tmp_path):
    vendor = tmp_path / "vendor"
    vendor.mkdir()
    (vendor / ".git").mkdir()

    cfg = RepoConfig(scan_roots=[str(tmp_path)], exclude=[str(vendor)])

    with patch("git.Repo", return_value=_make_repo()):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert results == []


def test_explicit_excluded_path_skipped(tmp_path):
    """Explicit path that also appears in exclude should be skipped."""
    repo_path = tmp_path / "secret"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)], exclude=[str(repo_path)])

    with patch("git.Repo", return_value=_make_repo()):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert results == []


# ---------------------------------------------------------------------------
# GitPython unavailable
# ---------------------------------------------------------------------------


def test_gitpython_unavailable_returns_empty(tmp_path):
    explicit_path = str(tmp_path)
    import sys  # noqa: PLC0415

    with patch.dict("sys.modules", {"git": None}):
        mods_to_drop = [k for k in sys.modules if "kpidash_client.repos" in k]
        for m in mods_to_drop:
            del sys.modules[m]
        from kpidash_client.repos import scan_repos  # noqa: PLC0415

        result = scan_repos(RepoConfig(explicit=[explicit_path]))
    assert result == []


def test_broken_repo_skipped_gracefully(tmp_path):
    """A repo that raises on Repo() construction should be skipped."""
    repo_path = tmp_path / "broken"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])

    with patch("git.Repo", side_effect=Exception("corrupt repo")):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert results == []


# ---------------------------------------------------------------------------
# Granular field tests (expanded repo data)
# ---------------------------------------------------------------------------


def test_new_fields_present(tmp_path):
    """All expanded fields appear with correct values."""
    repo_path = tmp_path / "myrepo"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    diffs = [_make_diff("M"), _make_diff("D"), _make_diff("R")]
    mock_repo = _make_repo(
        branch="feat",
        is_dirty=True,
        ahead_behind="3\t1",
        untracked=["a.txt", "b.txt"],
        wt_diffs=diffs,
        committed_date=1_700_000_000,
    )

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    r = results[0]
    assert r["ahead"] == 3
    assert r["behind"] == 1
    assert r["untracked_count"] == 2
    assert r["changed_count"] == 1
    assert r["deleted_count"] == 1
    assert r["renamed_count"] == 1
    assert r["detached_head"] is False
    assert r["default_branch"] == "main"
    assert r["last_commit_ts"] == 1_700_000_000.0


def test_detached_head_reported(tmp_path):
    """Detached HEAD is flagged and branch reported as 'HEAD'."""
    repo_path = tmp_path / "detached"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    mock_repo = _make_repo(branch="HEAD", is_dirty=False, detached=True)
    # Simulate TypeError from accessing active_branch on detached HEAD
    type(mock_repo).active_branch = property(
        lambda self: (_ for _ in ()).throw(TypeError("HEAD is detached"))
    )

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    assert results[0]["detached_head"] is True
    assert results[0]["branch"] == "HEAD"


def test_no_upstream_ahead_behind_zero(tmp_path):
    """Repos without upstream tracking report ahead=0, behind=0."""
    repo_path = tmp_path / "noup"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    mock_repo = _make_repo(branch="feat", is_dirty=True)
    mock_repo.git.rev_list.side_effect = Exception("no upstream")

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    assert results[0]["ahead"] == 0
    assert results[0]["behind"] == 0


def test_diff_type_counts(tmp_path):
    """Count change types correctly (M/A/T → changed, D → deleted, R → renamed)."""
    repo_path = tmp_path / "difftest"
    repo_path.mkdir()
    (repo_path / ".git").mkdir()

    cfg = RepoConfig(explicit=[str(repo_path)])
    diffs = [
        _make_diff("M"),
        _make_diff("A"),
        _make_diff("T"),
        _make_diff("D"),
        _make_diff("D"),
        _make_diff("R"),
    ]
    mock_repo = _make_repo(branch="dev", is_dirty=True, wt_diffs=diffs)

    with patch("git.Repo", return_value=mock_repo):
        from kpidash_client.repos import scan_repos

        results = scan_repos(cfg)

    assert len(results) == 1
    r = results[0]
    assert r["changed_count"] == 3  # M + A + T
    assert r["deleted_count"] == 2  # D + D
    assert r["renamed_count"] == 1  # R
