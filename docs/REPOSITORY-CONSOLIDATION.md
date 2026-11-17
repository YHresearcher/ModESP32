# Single source of truth — ModESP32

**Canonical GitHub repository:** https://github.com/YHresearcher/ModESP32

**Firmware + web flasher** live in this one repository. The former
`YHresearcher/agentesp32` is historical only; do not send patches, builds,
release manifests, or deployment changes to the old repository.

## Current architecture

| Component | Canonical location | Live address |
|---|---|---|
| ESP32-S3 firmware | `main/`, `scripts/`, `.github/workflows/mod-esp32s-*-firmware.yml` | Browser flash from website |
| Firmware Center frontend | `web-flasher/index.html`, `app.js`, assets, tests | https://agentesp32.hlthanh1992.workers.dev/ |
| Firmware Center API | `web-flasher/_worker.js`, `wrangler.jsonc` | Same website, `/api/firmware/*` |
| Chibi Gemini voice gateway | `cloudflare/chibi/` | https://chibi-esp32.hlthanh1992.workers.dev/ |
| Web deployment pipeline | `.github/workflows/agentesp32-web-flasher-deploy.yml` | Existing `agentesp32` Cloudflare Worker |
| Cloud gateway pipeline | `.github/workflows/chibi-cloudflare-deploy.yml` | Existing `chibi-esp32` Cloudflare Worker |

The `agentesp32` **Worker name and website URL intentionally stay unchanged**;
it does **not** require a GitHub repository with the same name.

## Migration audit (2026-11-17)

Compared complete recursively listed `main` trees of both repositories:
- Legacy `agentesp32`: 1,852 files.
- Canonical `ModESP32`: 1,892 files.
- **Zero files exist exclusively in the legacy repository.**
- Fourteen matching file paths have different content; canonical `ModESP32`
  contains extended Web Flasher profiles, API tests, Chibi changes and
  Cloudflare deployment workflow. Legacy files were **not blindly overwritten**.

This confirms **file coverage** in Git only; it does *not* prove that Cloudflare
is configured to build from this repo. The existing deployment workflow in
`ModESP32` uses `cloudflare/wrangler-action`; it can deploy without
a Cloudflare GitHub integration, provided its Actions secrets are configured.

## Release and website acceptance checklist

1. Run `Web Flasher Tests` in ModESP32; verify all pass.
2. Run `Deploy ModESP32 Web Flasher` in ModESP32. Deployment requires repository
   secrets `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID`.
3. Verify `https://agentesp32.hlthanh1992.workers.dev/` responds and
   `/api/firmware/latest` returns a validated release or an explicit
   `manifest_unavailable` when no corresponding firmware is published.
4. Confirm firmware release manifest points at **ModESP32** and expected board,
   verify SHA-256 in browser before enabling flash.
5. Check the Cloudflare Worker `agentesp32` has no conflicting automatic
   Git deployment from legacy repo `agentesp32` (Cloudflare Dashboard,
   Workers & Pages → Worker → Settings → Builds).
6. Retain legacy repo as a backup until a full deploy + web smoke test is
   verified. Then consider archiving or deleting legacy repository manually.
   **Never delete the Cloudflare Worker `agentesp32`** to remove the old repo.

## Scope and safety

The Firmware Center can use Chibi profile only once a matching firmware build
passes CI *and* relevant hardware checks. An online Worker or passing Web Flasher
test does not certify GPIO wiring, touchscreen calibration, mic or speaker.
Do not distribute credential-bearing firmware: the public Chibi build does not
embed `CHIBI_DEVICE_TOKEN`. Keep protected token provisioning separate.

## Development rules

- Change website only under `web-flasher/` here.
- Change firmware only under `main/`, `scripts/`, and appropriate workflows here.
- Publish release/manifest and deploy website from this repo.
- Do not synchronize the legacy repo in either direction.
