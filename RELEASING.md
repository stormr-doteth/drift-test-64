# Releasing a new version

1. Test the ROM on real hardware (`./build.sh`, copy `drift-test-64.z64`
   to the SD card):
   - every button turns green, stick chart fills in
   - hold START → menu opens
   - SAVE REPORT TO SD → `drifttest64/report_NNN.html` appears and prints
   - SCREEN SIZE change is remembered after a power cycle
2. Commit and push to `main`. The **Build** action must pass.
3. Tag and push the version:

   ```sh
   git tag v1.0.0
   git push origin v1.0.0
   ```

4. GitHub Actions builds the ROMs and publishes a Release with
   `drift-test-64.z64` and `drift-test-64-debug.z64` attached.
   Edit the release notes to say what changed and which flashcarts were tested.
