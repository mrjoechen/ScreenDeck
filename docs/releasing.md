# Landing page and releases

The static site under `site/` documents the firmware and uses ESP Web Tools
10.4.0 for USB Web Serial flashing. It has no frontend build step and uses
relative URLs so it can be published as a GitHub Pages project site.

Public installer: https://mrjoechen.github.io/ScreenDeck/

## Local preview

Do not open `index.html` as a `file://` URL. Serve the directory over HTTP:

```sh
python3 -m http.server 4173 --directory site
```

Then open `http://localhost:4173/`. A local preview needs a merged factory
image at:

```text
site/firmware/esp32-s3-4848s040/screendeck-esp32s3-4848s040-factory.bin
```

Generate it with the steps in [Building and flashing](build-and-flash.md).
The binary is gitignored.

The device registry is `site/firmware/devices.json`. Each future board must
use its own directory and ESP Web Tools manifest. Chipset detection can
distinguish ESP32 families, but it cannot distinguish two incompatible display
boards that both use ESP32-S3.

## Tagged releases

Pushing to `main` does not build firmware or update GitHub Pages. Publishing
happens only when you push a version tag:

```sh
git tag v1.0.0
git push origin v1.0.0
```

`.github/workflows/release.yml` then:

1. Compiles firmware with `SCREENDECK_VERSION` (the tag) and
   `SCREENDECK_BUILD_TIME` (UTC timestamp) baked in.
2. Merges a factory image and publishes GitHub Release assets.
3. Deploys `site/` so the browser installer flashes that tagged image.
4. Writes version, channel, and update date into `site/firmware/release.json`,
   which the landing page reads for the version and updated fields.

Tag names should match `v*`, for example `v1.0.0`.

In the repository: **Settings → Pages → Source → GitHub Actions**. The first
successful tagged run publishes https://mrjoechen.github.io/ScreenDeck/

The release workflow deploys Pages from the tag job without attaching the
`github-pages` environment. That environment is created with a default-branch
rule, which rejects tags before any step runs. The deploy job uses
`pages: write` and `id-token: write` instead.

If a previous run already created the environment and you still want reviewers
or branch limits, open **Settings → Environments → github-pages** and set
**Deployment branches and tags** to **No restriction**, or add a tag rule `v*`.
