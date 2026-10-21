# ember-wifi

A small Wi-Fi manager for EmberBSD desktops: a tray icon with the
connection state and a window listing the networks in range. While connected
or negotiating a connection, refresh reads the saved scan results. It does
not start an active scan, which can interrupt station-mode drivers. Active
scans run only in the disconnected or inactive state. Picking a
network asks for its password and saves it in `wpa_supplicant`
(`update_config=1` is required in its configuration). It replaces
`dhcpcd-gtk`, whose network list did not open on the MSI.

It talks to the `wpa_supplicant` control socket in
`/var/run/wpa_supplicant` directly, so passwords never appear on a
command line. Supported: open and WPA/WPA2-PSK networks (WPA2/WPA3
transition networks connect over WPA2). WPA3-only, Enterprise and WEP
networks are shown as unsupported.

Build (Qt 6 Widgets, pkgsrc):

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/usr/pkg/qt6
    ninja -C build && ./build/wpa_test

`wpa_test` checks the parsing of control-interface replies and the
commands sent for a new network (48 checks). Installed on the MSI as
`/usr/local/bin/ember-wifi`, started from
`/root/.config/autostart/ember-wifi.desktop`.
