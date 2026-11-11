#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
umask 077
cd -- "${repo_root}"
exec op run --env-file="${repo_root}/ansible/secrets.env.refs" -- \
  ansible-playbook -i localhost, "${repo_root}/ansible/deploy.yml" "$@"
