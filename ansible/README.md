# Alarm panel deployment

The Ansible playbook builds the real ESP32 firmware and can upload it using
Arduino OTA. It is separate from ESPHome Builder; the panel uses custom Arduino
firmware, MQTT and ArduinoOTA.

## Prerequisites and credentials

Install Ansible, PlatformIO (`pio`), Python and the 1Password CLI (`op`) on the
machine running the playbook. The existing local project is the firmware source;
no repository clone or remote URL is executed by the deployment. The current
git revision and dirty state are recorded in each artifact manifest.

`ansible/secrets.env.refs` contains references only. It selects
`GREATWHITE-IoT` and `op://Homelab/UniFi WiFi/iot_password`. MQTT credentials,
client ID and the existing OTA hostname/password remain in the `Homelab/Alarm
Panel` item. No credentials need to be pasted into YAML or CLI arguments.
The deployment uses the broker IPv4 address 192.168.2.91:1883 from `vars.yml`,
overriding the stored `homeassistant.local` hostname, which depends on mDNS.

The existing OTA password must match the currently installed firmware. This
workflow retains it in the new firmware; changing that field is a separate
credential-rotation operation. The existing standalone renderer/`just flash`
workflow remains available and still reads that item's legacy Wi-Fi fields;
use this Ansible workflow for the IoT migration and subsequent deployments.

## Preview and build

Run from the project root:

```sh
bash scripts/deploy.sh --check
bash scripts/deploy.sh
```

The first command previews the workflow; it does not build/upload. The second
builds only. Ansible's check mode does not prove compilation or network access.

Firmware is compiled for `esp32dev`, not `esp32dev-wokwi`. Source files are
copied to a temporary workspace, excluding any existing `include/secrets.h`.
The renderer writes a mode-0600 header in that workspace; it is removed with the
workspace on success/failure. Existing source-tree headers are not overwritten.
The build can download PlatformIO dependencies and update PlatformIO's cache.

Artifacts are stored under ignored `.artifacts/firmware-*/`: `firmware.bin` and
`manifest.json`. Firmware embeds credentials, so these private artifacts must
not be committed or shared. Directories use mode 0700 and files use mode 0600.
The manifest records the source revision, firmware SHA-256, size, non-secret
target configuration, and whether an upload was acknowledged.

## Prepare the migration network

Before uploading a Wi-Fi change:

1. Reserve a stable IoT IP for the panel MAC in the infrastructure repo.
2. Permit only that panel IP to reach the MQTT broker at 192.168.2.91 TCP 1883.
   Existing IoT isolation otherwise blocks panel-initiated access to the LAN.
3. For future OTA from Home to IoT, permit that panel IP to reach the deployment
   machine's LAN IP on TCP 3232. ArduinoOTA sends an invitation to panel UDP 3232,
   then the panel opens a new TCP connection back to the uploader. This callback
   is not covered by the state for the UDP invitation. Home-to-IoT is already
   allowed by the current router policy. Keep this exception restricted to the
   fixed deployment host and port; the uploader binds to that IP/port.
4. Allow the deployment program through that host's firewall if needed. Use a
   current routable IPv4 target, not an mDNS `.local` name across VLANs.

Use the current Home address for the first upload; the firmware will reboot onto
IoT afterward. MQTT topics and alarm commands are unchanged by this workflow.

## Upload

Create an ignored `ansible/local.yml`, using the real addresses:

```yaml
alarm_panel_current_host: "CURRENT_PANEL_IP"
alarm_panel_expected_ip: "RESERVED_IOT_IP"
alarm_panel_deploy_host_ip: "UPLOADER_LAN_IP"
```

Build/upload explicitly:

```sh
bash scripts/deploy.sh -e @ansible/local.yml -e alarm_panel_upload=true
```

The uploader calls the installed PlatformIO ESP32 `espota.py` helper in-process.
The OTA password stays in memory/environment rather than appearing in process
arguments. Ansible hides secret-bearing task output. The upload has a fixed
callback TCP port of 3232 rather than a random port. Uploading deliberately
reboots the panel; run it when a brief panel outage is acceptable.

Afterward, confirm the new DHCP lease and MQTT `home/keypad/status` online event,
and verify the display's alarm/garage state. `uploaded: true` means the firmware
transfer was acknowledged; it is not proof of post-reboot MQTT/alarm operation.
Update `alarm_panel_current_host` to the reserved IoT IP for later updates.
If credentials/network settings prevent reconnection, USB flashing is the
recovery path; this firmware does not provide a Wi-Fi fallback portal.

## Offline checks

```sh
python3 scripts/test_deployment.py
ansible-playbook -i localhost, ansible/deploy.yml --syntax-check
```

The offline tests do not compile or upload hardware. The private artifact
manifest makes source revisions and firmware hashes available without exposing
resolved secrets. No schedule, credential rotation, automatic device discovery
or alarm command is performed by this playbook.

## Confirmed TARS migration addresses

UniFi identified Alarm Panel (TARS) at 192.168.2.231 on GREATWHITE, MAC
d8:13:2a:73:c6:e8. The infrastructure repo now prepares an unused IoT reservation
at 192.168.20.50 and scoped MQTT/OTA callback rules. These infrastructure changes
must be reviewed/applied before the firmware migration. The current uploader
LAN IP is 192.168.2.169 and callback port is TCP 3232.

Defaults target the final reserved IoT address for future uploads. For the
first migration, explicitly override the current (old) panel address:

```sh
bash scripts/deploy.sh -e alarm_panel_upload=true \
  -e alarm_panel_current_host=192.168.2.231
```

After the panel moves to IoT and is verified, subsequent deployments use:

```sh
bash scripts/deploy.sh -e alarm_panel_upload=true
```

Hardware firmware has been built successfully with the new Wi-Fi credentials;
no OTA upload has been performed by the agent. Migration still needs the
router changes followed by this explicit upload and MQTT/UI verification.
