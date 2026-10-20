# Network of your city

The radio profile of a local MeshCore network (frequency, bandwidth, SF, CR) can be picked from a catalogue of
cities instead of typing it: on the website, on the device page and in the Android app. The catalogue is kept by
the community: [VoxMesh/meshcore-local-networks](https://github.com/VoxMesh/meshcore-local-networks). Report
mistakes and new cities there through Issues.

## What changes

Only the four parameters a network shares: frequency, bandwidth, SF and CR. Power, relaying, the hop limit, the
region, keys and contacts stay. Installing firmware never changes radio settings. A profile is written to a board
only when the user has chosen a network and confirmed the write.

MeshMesh works in 863–870 MHz with a bandwidth of 62.5, 125, 250 or 500 kHz. Networks outside that range (433 MHz,
for example) are listed but cannot be written to a MeshMesh board.

## Catalogue

- A copy is kept in `site/networks.json`. The site loads it next to the page; the app carries it in the APK.
- "Update the catalogue" downloads the current `catalog.json` from GitHub and keeps it in the browser (or the app's
  WebView). The newer of the two copies by `updated` is used. Nothing is downloaded on every start.
- `tools/networks.py update` refreshes the copy, `tools/networks.py check` validates it (part of the Pages checks).
- The list is sorted by distance when the position is known, otherwise in catalogue order. Each place shows its
  networks and those of its parent territory (`parent_id`). The search matches the city and region names.
- The position comes from the board's GPS (a trusted fix only), from the phone in the app (an approximate one is
  enough) or from the browser. The board's page opened over Wi-Fi (http) cannot get the browser's position: find
  the city by name.

## Website

The "Network of your city" box is in the install section (`site/netpick.js`):

1. Choose a network. The choice stays in the browser (`localStorage`) across page reloads, the next board, a repeated
   installation or another board type, until you press "Clear". If the catalogue changes the network's settings, the
   site shows the new ones and offers to use them.
2. Close the install dialog (the port must be free) and press "Connect a board over USB" or "Over Bluetooth". The site
   talks the MeshCore companion protocol, so it works with MeshMesh 0.9.0+ and the stock MeshCore companion alike.
3. The site shows the board's name, model and firmware, its current and the new settings. When the network is in another
   band than the board's current frequency (433 / 868 / 915 MHz), writing needs the box "The board and antenna are
   made for … MHz". A MeshMesh board cannot get a frequency outside 863–870 MHz.
4. "Write to this board" confirms each board. The site checks what was written, keeps the board's previous settings
   (by its key) and disconnects, so the next board can be connected. Connecting the same board again offers "Return
   the previous ones".

The stock MeshCore client repeat flag (firmware 9+) is sent back as it was.

## Device page and app

"Settings → Radio" has "Choose a network by city" (`renderNet` in `web/index.html`). A chosen network shows what will
change; "Apply" takes two presses, as other risky actions of the page.

With firmware 0.27.0 there is "Test": the settings are applied but not saved.

- **Keep** saves them.
- **Revert** returns the saved ones.
- **Check** asks the repeaters that hear the board directly (the request of the region search) and shows how many
  answered. A person decides: there is no automatic rollback on silence.

A restart during a test brings the saved settings back.

The board remembers the profile before the last change of frequency, bandwidth, SF or CR. "Return the previous" goes
back to it, and the current one becomes the previous. Changes over the companion protocol (MeshCore apps, the site)
count too.

## Board

- USB/BLE/HTTP commands (`/api/command`): `radio try {"frequency":868.731,"bandwidth":62.5,"sf":7,"cr":7}`,
  `radio keep`, `radio revert`, `radio previous` (`src/App.cpp`, `radioCommand`).
- `config` gives `radio_prev` (the previous profile) and `radio_trial` (the saved profile during a test).
- A test lives in memory only: `Config::save` writes the saved profile to NVS until the test is kept. The previous
  profile is kept in NVS under `radio_prev`.
- On the screen: M9 and T-Deck have "Previous radio" / "Radio test" in the radio settings (OK returns or keeps it).
  One-button boards, GAT562 and T114 have "Previous" / "Test" in the radio editor (hold).

## Checked

- Firmware built for M9, Heltec V4, Heltec V3, T-Deck, GAT562 and T114; the radio screens checked in `tools/ui_preview`.
- Device page: choosing, testing, Keep, Revert and Return the previous checked in a browser with `tools/web_demo.py`.
- Website: the choice survives a page reload.
- Not checked on boards: the `radio …` commands, writing from the site over USB/Bluetooth, and the phone position in
  the app. No board was connected during development.
