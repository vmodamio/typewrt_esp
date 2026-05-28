# Typewrt GitHub Import Helper

Use `make_typewrt_manifest.py` when you have a folder on your PC that you want to move
to Typewrt through a new GitHub repository while preserving file modification times.

Git does not store per-file mtimes, so the helper writes `.typewrt-sync.json` next to
your files. The Android companion reads that manifest, restores mtimes in its `remote/`
mirror, then sends files to Typewrt with `TYPEWRT-FILE2`. The firmware applies those
timestamps on the SD card.

## Procedure

1. Copy or move your files into a clean folder.

2. From that folder, generate the Typewrt manifest:

   ```sh
   python3 /path/to/typewrt_nextvi/utils/make_typewrt_manifest.py .
   ```

3. Create and push a new Git repository:

   ```sh
   git init
   git add .
   git commit -m "Initial Typewrt files"
   git branch -M main
   git remote add origin git@github.com:YOUR_USER/YOUR_REPO.git
   git push -u origin main
   ```

4. In the Android companion, configure:

   ```text
   Repository: YOUR_USER/YOUR_REPO
   Branch: main
   Path: leave blank unless you committed the files inside a custom subfolder
   ```

   If the repository contents are wrapped in a single top-level folder with the same
   name as the repository, the companion treats that folder as the Typewrt root.

5. Tap the GitHub pull action. The companion will use `.typewrt-sync.json` for mtimes.

6. On Typewrt, open the menu in the target SD-card directory, run `ble recv`, then tap
   **To Typewrt** in the companion.

## Notes

- Run the helper again before committing later PC-side mtime-sensitive updates.
- `.typewrt-sync.json` should be committed with the files.
- If the manifest is missing, the companion falls back to GitHub's latest commit date
  for each file, which is less precise for bulk imports.
- FAT stores modification times at about 2-second precision, so exact odd seconds may
  not round-trip perfectly.
