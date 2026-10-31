Introduction
============

All items on this list must be completed for a successful release.


Details
=======

* Ensure Sigil builds and runs.
* Identify the previous enhanced release tag and audit the full `previous-tag..HEAD` range.
* Write the release announcement and `docs/ReleaseNotes-<version>.md` from that range.
* Add a new top section to `ChangeLog.txt`; never append post-release work to an already tagged version section.
* Link the current release notes from `README.md`.
* Update translations.
* Ensure Qt is at the latest version on all OSs packages will be built for.
* Bump the enhanced version in `CMakeLists.txt`.
* Bump `version.xml` to the matching dotted enhanced version.
* Verify the CMake, `version.xml`, ChangeLog, release-notes, and tag versions agree.
* Record the release date in the release notes (and in `ChangeLog.txt` if its format includes dates).
* Commit version changes after validation.
* Push the validated mainline first, then create and push the annotated release tag using the established `vX.Y.ZEN` form.
  * For 2.8.5E13: `git push enhanced master`, then `git tag -a v2.8.5E13 -m "Sigil-Enhanced 2.8.5E13"` and `git push enhanced refs/tags/v2.8.5E13`.
  * The tag must point at the same commit as the published mainline. Tag pushes trigger `.github/workflows/package-enhanced.yml` for Windows/macOS packaging, asset checksums, and the GitHub Release.
* Build source package $ git archive --prefix Sigil-x.y.z/ -o ../Sigil-x.y.z-Code.zip HEAD
* Build packages.
  * OS X
  * Windows
  * Sign packages
    * OS X
* Generate Checksums file (sha256).
  * `shasum -a 256 * > Sigil-x.y.z-CHECKSUMS.sha256`
  * Remove the first line which is the checksum for the empty checksum file produced due to the globing.
* Make Release on GitHub
  * Upload packages
    * Code
    * OS X
    * Windows
    * SHA256 sums
* Post release announcement on Blog(s).
* Post release announcement on MobileRead.
* Push git changes.
