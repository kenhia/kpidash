_default:
    @just --list

# Build and publish kpidash-client to the homelab package store
# (see k-homelab docs/deploying.md). kpkg refuses an already-published
# version — bump clients/kpidash-client/pyproject.toml first.
publish:
    #!/usr/bin/env bash
    set -euo pipefail
    cd clients/kpidash-client
    rm -rf dist && uv build
    d=$(ssh -n kubsdb mktemp -d)
    scp dist/* kubsdb:"$d"/
    ssh -n kubsdb "kpkg add $d/* && rm -rf $d"
