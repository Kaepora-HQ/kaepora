# Releasing Kaepora

release-please keeps one pull request open, titled `chore(main): release <version>`. It raises the version on the `project()` line of `CMakeLists.txt` and in `.release-please-manifest.json`, and adds the release's section to `CHANGELOG.md`. A `feat` commit raises the minor version and a `fix` commit raises the patch version. `ci`, `build`, `test`, and `chore` commits stay out of the changelog, and release-please opens no release pull request for them alone. Neither do commits that touch only `web/`.

## What merging the release pull request does

Squash-merge the release pull request. `.github/workflows/release.yml` runs on every push to `main`, one run at a time. On the merge's push, release-please tags the merge commit `v<version>` and creates the release as a draft. Every run then looks for a draft of the version in `main`'s manifest. While there is one, the run:

1. Builds and tests the draft's tagged commit on Linux, Windows and macOS, with the jobs of `check.yml`.
2. Signs `kaepora.exe` and zips the Windows package again. It makes the Windows installer from the same folder, so the installer holds the signed `kaepora.exe`, with the Inno Setup that `tools/install-inno-setup.ps1` pins and installs from upstream's release, and signs the setup file. The uninstaller, which Setup writes on the player's PC, and the copy of itself that Setup unpacks into TEMP and runs are not signed. The macOS app keeps the ad hoc signature its build gave it.
3. Starts the signed package on a fresh Windows runner. On another, it installs the signed installer, starts Kaepora from where it installed, and uninstalls it.
4. Writes `SHA256SUMS` and `latest.json`.
5. Uploads the files to the draft and publishes it.

Players see the release only after step 5. Until then, the website's download buttons and the window's update notice point at the previous release.

The first release, 0.1.0, is the first time release-please's tag and draft, the Azure login, the signature, the upload, and the publish run, because they run only on `main`.

## Check the release before you merge it

The workflow's own token opens the release pull request. GitHub may hold that pull request's checks until you approve them, as it does in `mx`, but whether it does so here while the repository is private is not known yet. Approving them builds and tests the release commit and writes its files, without signing or publishing. Either way, the tests that read the game file cannot run on GitHub.

1. Read the release's section of `CHANGELOG.md` in the pull request.
2. Run every test, with the game file, on the pull request's commit:

	```sh
	gh pr checkout <number>
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKAEPORA_GAME_NUP=/path/to/game.nup
	cmake --build build
	ctest --test-dir build --output-on-failure
	```

## If a release does not finish

The release stays a draft until the last step publishes it, and until then nothing that players see changes.

- If a runner, GitHub, or Azure failed, run the failed jobs again with `gh run rerun <run id> --failed`, or push the next commit to `main`. Either run finishes the draft. A run that finds the release already published changes nothing.
- If `release.yml` or `check.yml` is at fault, merge the fix to `main`. Its push runs the fixed workflow, which finishes the same draft.
- If the program, a script under `tools/`, or the installer's script `dist/kaepora.iss` or its icon is at fault, the tagged commit holds the fault, and that version cannot ship. Delete the draft with `gh release delete v<version>`, which keeps the tag, and then merge the fix. release-please finds the failed release by its tag and lists the fix in the next release pull request. A fix typed `ci` or `build` waits for the next `feat` or `fix` commit.

Never rewrite `main` after a release is tagged. release-please finds the last release by its tag.
