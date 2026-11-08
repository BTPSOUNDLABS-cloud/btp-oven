# Putting BtP Oven on GitHub

A checklist for the first push. Takes about ten minutes.

The important rule: **the repo holds source and docs. The packaged builds go on
a Release, not in a commit.** GitHub blocks any single file over 100 MB, so a
committed zip will fail to push anyway.

There are two packages:

| Package | Build | Contents | Can be sold? |
|---|---|---|---|
| `BtP-Oven-1.0.0.zip` | `build` | RAVE included | No (CC-BY-NC) |
| `BtP-Oven-1.0.0-commercial.zip` | `build-commercial` | no RAVE | Yes, under JUCE Indie |

---

## 1. Create the repo (once)

From the project root:

```powershell
git init
git add .
git commit -m "BtP Oven 1.0.0"
```

`.gitignore` already excludes `build/`, `dist/`, `__pycache__/`, model weights
(`*.gguf`, `*.onnx`, …) and audio files, so the commit stays small.

Check the size before pushing:

```powershell
git count-objects -vH
```

A clean first commit should be a few MB — source, docs, licence texts. If it is
hundreds of MB, something slipped past `.gitignore`; run `git status` and look
for `.onnx` / `.gguf` / `.wav` files.

## 2. Push

```powershell
git branch -M main
git remote add origin https://github.com/<you>/btp-oven.git
git push -u origin main
```

## 3. Put a build on a Release

GitHub repo → **Releases** → **Draft a new release**

- Tag: `v1.0.0`
- Title: `BtP Oven 1.0.0`
- Attach `dist\BtP-Oven-1.0.0.zip` (2 GB limit per asset — 785 MB is fine)
- Paste the SHA-256 into the notes so people can verify the download.

Generate the hash when you package, so it always matches the file you upload:

```powershell
powershell -ExecutionPolicy Bypass -File Packaging\Package-BtPOven.ps1 -Models Core -Zip
Get-FileHash dist\BtP-Oven-1.0.0.zip -Algorithm SHA256
```

Publishing the release does **not** add the file to the git history. That is
the point.

The commercial package is a separate asset (or a privately hosted download):

```powershell
powershell -ExecutionPolicy Bypass -File Packaging\Package-BtPOven.ps1 `
           -Models Core -NoRave -BuildDir build-commercial -Version "1.0.0-commercial" -Zip
```

Do not attach the commercial zip to a public free release unless you intend to
give the sellable build away.

## 4. Say what the licence is, up front

GitHub shows the licence in the sidebar. Since this is source-available rather
than open source, add the repo **Description** and a one-line note at the top of
the README so nobody assumes MIT:

> Source available. Free to build and use, including on music you sell.
> Not for resale. The free build contains CC-BY-NC components — see LICENSING.txt.

Leave GitHub's "Choose a licence" picker alone. Picking MIT or GPL there would
contradict `LICENSE` and confuse anyone auditing the project.

## 5. Optional polish

- Move a few good screenshots into `docs/screenshots/` and reference them in the
  README. Root-level PNGs are ignored on purpose; that folder is not.
- Add a `CHANGELOG.md` before the second release, not after.
- Pin an issue called "Licence questions" so the answer is in one place.

---

## Checklist

- [ ] `git count-objects -vH` is small (no models, no audio, no dist)
- [ ] `LICENSE` present at the root and visible in the GitHub sidebar
- [ ] README licence section still accurate
- [ ] `Packaging/THIRD-PARTY.txt` and `Packaging/LICENSES/` committed
- [ ] Release asset attached, SHA-256 in the notes
- [ ] Repo description states source-available + NC component

---

## Selling the commercial build

The source already supports a sellable build, so selling does not mean editing
the code by hand:

1. Keep the JUCE Indie licence active (covers up to USD 300k/year).
2. Configure with `-DBTP_ENABLE_RAVE=OFF` and build `build-commercial`.
3. Package with `-NoRave -BuildDir build-commercial`. The package has no RAVE
   code, no RAVE weights and a `NO-RAVE.txt` marker.
4. Re-run the audit in `Packaging/THIRD-PARTY.txt`. Anything marked NC must be
   absent from that package.
5. The public/free release stays the personal build; the commercial zip is the
   one you charge for.

Step-by-step commands are in `Packaging/BUILD-VARIANTS.txt`.
