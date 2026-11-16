# ModESP32 Firmware Center (experimental)

Browser-based ESP32-S3 N16R8 Firmware Center, source in `web-flasher/`. Hosted website: https://agentesp32.hlthanh1992.workers.dev

## Supported workflow

1. Go to [ModESP32 S3 Firmware (Web Flash)](https://github.com/YHresearcher/ModESP32/actions/workflows/mod-esp32s3-firmware.yml) and run it on main, or use its most recent **successful** build. Download the artifact `mod-esp32s3-st7789v-28-<sha>` and unzip it.
2. On the website, select **merged-binary.bin**. Optionally select **SHA256SUMS** from the same artifact to verify the digest (strongly recommended). If a digest does not match, flashing is blocked.
3. Confirm the actual ESP32-S3 N16R8 board type, the LCD/amp power wiring and any important configuration backups. Only then does the Flash button become enabled.
4. Use Chrome or Edge desktop with a USB data cable; close any program using the serial port. Click **Connect & Flash Firmware** and follow ESP Web Tools instructions. ESP Web Tools detects the chip family and should reject a non-S3 device. The merged image is flashed at offset 0.
5. Close ESP Web Tools before opening the separate Serial Monitor. Select 115200 baud; press RESET/EN to read boot logs. The monitor can label possible panic, PSRAM, display, I²S and Wi-Fi errors. These are keyword hints, **not proof of root cause**.
6. **Download log** stores the raw log locally. **Create redacted preview** makes an editable text excerpt; inspect it carefully. Only after checking the consent box can you open a draft public GitHub Issue. The website does not submit the issue by itself.

## What is validated, and what is not

- The web app rejects files that are not named `merged-binary.bin`, are too small/large for a 16MB image, or do not have ESP image magic 0xE9 at flash offset 0.
- SHA-256 verifies **integrity relative to the supplied SHA256SUMS file**, but does **not independently authenticate** an artifact obtained from an untrusted source. Both files should come from the intended successful GitHub Actions run.
- Naming, size and header checks do **not** guarantee the image matches the board, that every embedded partition is correct, or that it is safe to flash. The browser and ESP Web Tools add chip-family checking, but there is still risk with unsuitable hardware and incorrect images.
- The device's LCD ST7789V module supply and LED backlight drive, resistive-touch controller, microphone chip and GPIO mapping have not yet been electrically verified. The backlight and touch remain intentionally disabled in this first board version.
- Neither the static website nor GitHub Actions can read the physical ESP32 without user-initiated local USB access.
- The site requires HTTPS and Web Serial. Flash and Serial Monitor cannot hold the same port concurrently.

## Privacy

Nothing is automatically uploaded from Serial Monitor. Logs can include SSIDs, IP/MAC addresses, API tokens, device identifiers, conversation snippets and other sensitive data. Basic client-side redaction masks common patterns but is **not comprehensive**. GitHub Issues in this public repository are public, so review and edit the preview before any submission. For sensitive errors, prefer privately sharing a suitably redacted file.

## CI

- .github/workflows/mod-esp32s3-firmware.yml: ESP-IDF 6.1 build **only** for ModESP32 S3; artifact contains merged-binary.bin and SHA256SUMS, kept for 14 days.
- .github/workflows/web-flasher-tests.yml: tests for browser JavaScript parsing and pure diagnostics functions.
- .github/workflows/build.yml: upstream broad board CI, now skips web-only changes to avoid expensive unrelated board builds. Manual runs still exercise the full matrix.

## Cloudflare Pages / Vercel deployment

Deploy static folder `web-flasher` on branch `main`, framework None, output directory `.` within that root. Cloudflare Workers deployments can also serve the same files, but must be updated through your own Cloudflare deployment setup; a GitHub commit alone doesn't prove the live website has redeployed.

ESP Web Tools documentation: https://esphome.github.io/esp-web-tools/
