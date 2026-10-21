# Troubleshooting

[← docs](README.md)

Each entry here happened on a real build, along with what fixed it.

## Flashing

**No port appears in the web flasher / `pio` can't find the board.**
Use a USB-C cable that carries data (many only charge). Hold **BOOT** while plugging the board in, then try
again. On an assembled lynx, the base socket may not pass data. Flash the XIAO bare, or through its own port.

**`just fw-flash` picks the wrong port.** `just fw-ports` lists them; then
`LYNX_PORT=/dev/cu.usbmodem1101 just fw-flash`.

## Wi-Fi and setup

**The setup page doesn't pop up on the phone.** Browse to `http://192.168.4.1/` while joined to the
`lynx-xxxx` hotspot. Some phones also need mobile data turned off for that.

**My network isn't in the list.** lynx is 2.4 GHz only. A 5 GHz-only network won't appear.

**It won't join.** The setup page reports *Couldn't join* after 20 seconds and the hotspot stays up, so
you can retry. Over serial, `wifi <ssid> <password>` does the same.

**`lynx-xxxx.local` takes ~5 s to load, or never does (macOS / iOS).** Fixed in firmware 0.3.0: Apple
devices ask mDNS for an IPv6 address first, and the board now has one. On older firmware, use the IP.
On **Android**, `.local` names often don't resolve at all. Use the IP shown on the setup page or in your router.

**Lost the token.** Hold **BOOT** for 3 seconds: the setup hotspot reopens for 10 minutes and its page
shows the token. Or type `token` on the serial console. `token new` rotates it; Home Assistant then
asks for the new one.

## Camera

**Every frame is black.** Check the room first: at night with the lights off, the sensor returns black
frames with faint noise. Then check that the lens film is off.

**The picture is upside down or mirrored.** `POST /api/camera {"vflip": true}` (or `hmirror`), or the
*Flip image* / *Mirror image* switches in Home Assistant. The setting is saved.

**The stream says 503.** Port 81 is full: 4 connections in all, counting live viewers and `/events`
subscribers (Home Assistant holds one). Close a viewer. Snapshots on port 80 always work.

## Detection and follow

**Detection never sees anyone.** Set `range` to `far` (the default): with `near`, a face has
to fill ~30 % of the frame. Check the light, too. To test it without anyone in front of the camera,
send a photo to `/api/detect/image`, and add `?debug=1` to see exactly what the model got.

**Follow turns the wrong way.** That's how the board sits in the pod. Run `follow signs -1 1` or
`follow signs 1 -1` on the serial console (or `POST /api/follow {"panSign": 1}`). It's saved.

**Follow fights my manual moves.** Any manual move pauses follow for 5 s, and a sweep for 30 s.
Turn follow off to keep the head still.

## Motion

**The head doesn't move on battery.** That's expected: the servos run only from USB 5 V. The camera
stays up on the LiPo.

**It resets when it moves fast.** That's a brownout. Use a 2 A USB supply and a short cable, and check that the
470 µF capacitor is across the servo supply, negative to GND.

**One side has less travel than the other.** The horn went on off-centre. `look 0 0` and refit it,
then fine-tune with `trim` (see [calibration](../firmware/lynx/README.md#calibration)).

**Tilt goes the wrong way.** `tilt 45` must lean the lens toward the front. If it goes back, run `invert tilt` then `save`.

## Home Assistant

**lynx isn't discovered.** Discovery needs firmware 0.3.0 or newer (it advertises `_lynx._tcp` with its
id), and HA on the same network segment. Otherwise use *Add integration › lynx* with the IP.

**Live views stall with several dashboards open.** That's the 4-connection limit on port 81 (see above).
Use picture cards (snapshots) for the extra ones.
