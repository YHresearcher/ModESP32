#!/usr/bin/env bash
# Read-only deployment preflight for Chibi Cloud. No keys or secrets printed.
set -u
PROJECT="${1:-bidding-system-508101}"
REGION="asia-southeast1"
echo "=== Chibi Cloud preflight ($PROJECT) ==="
if ! command -v gcloud >/dev/null 2>&1; then
  echo "ERROR: gcloud not installed; open Google Cloud Shell"; exit 2
fi
echo "Account:"
gcloud auth list --filter=status:ACTIVE --format='value(account)' || true
echo "Project visibility:"
gcloud projects describe "$PROJECT" --format='table(projectId,projectNumber,lifecycleState)' || true
echo "Billing state (permission required):"
gcloud beta billing projects describe "$PROJECT" --format='yaml(billingAccountName,billingEnabled)' 2>&1 || true
echo "Enabled required APIs:"
gcloud services list --enabled --project="$PROJECT" --filter='config.name:(run.googleapis.com cloudbuild.googleapis.com artifactregistry.googleapis.com secretmanager.googleapis.com)' --format='value(config.name)' || true
echo "Active identity IAM policy entries (read permission required):"
ACCOUNT="$(gcloud auth list --filter=status:ACTIVE --format='value(account)' | head -n 1)"
if [ -n "$ACCOUNT" ]; then
  gcloud projects get-iam-policy "$PROJECT" --flatten='bindings[].members' --filter="bindings.members:user:$ACCOUNT" --format='table(bindings.role)' 2>&1 || true
fi
echo "Existing Cloud Run service, if any:"
gcloud run services describe chibi-gateway --project="$PROJECT" --region="$REGION" --format='yaml(status.url,status.conditions)' 2>&1 || true
echo "=== End preflight. Share output only after reviewing it; do not share tokens or keys. ==="
