# iPhone-only update workflow

Once this repository is on GitHub:

1. Open the repository in Safari on your iPhone.
2. Tap **Actions**.
3. Open **Build Waveshare Ghost Firmware**.
4. Tap **Run workflow**.
5. Wait for the green check mark.
6. Open the completed workflow run.
7. Under **Artifacts**, download **ghost-waveshare-s3-can**.
8. In the Files app, unzip the artifact.
9. Connect the iPhone to the ESP32 Wi-Fi network.
10. Open the ESP32 dashboard in Safari.
11. Open **Firmware Update**.
12. Select `ghost-waveshare-s3-can.bin`.
13. Upload it and wait for reboot.

After installing this custom v0.1 build, connect to:

- SSID: `Tesla-Ghost`
- Password: `ghost2018`
- Dashboard: `http://192.168.4.1`

This v0.1 build is CAN listen-only.
