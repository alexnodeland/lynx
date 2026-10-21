// SPDX-License-Identifier: MIT
// The build guide's content: from printed parts to a camera on your network.
// `add` names the parts (glTF holders, without _Move) that arrive in a step; everything from
// earlier steps stays in place, later ones show as a faint ghost. `from` is where each part flies
// in from, in Blender part-local mm. Facts here follow hardware/production/BOM.md.

export const GUIDE = [
  {
    id: "start",
    chapter: "Prepare",
    title: "What you'll need",
    time: "5 min",
    show: "all",
    body: `lynx is eight printed parts, a XIAO ESP32-S3 Sense, two micro servos and one kind of screw.
      Plan on about an hour and a half for the build, plus printing time.`,
    need: [
      ["1", "Seeed XIAO ESP32-S3 Sense", "with camera, heatsink, FPC antenna"],
      ["2", "MG90S micro servo", "Waveshare 15024, or Adafruit SG92R"],
      ["9", "M2 × 8 thread-forming screw", "pan head (McMaster 99461A916)"],
      ["1", "USB-C panel socket", "Adafruit 6069"],
      ["1", "Right-angle USB-C cable", "thin, data-capable, 15–20 cm"],
      ["1", "470 µF 6.3 V capacitor", "Panasonic EEU-FR0J471"],
      ["1", "12.7 × 3.2 mm magnet", "K&J D82"],
    ],
    tools: ["PH0 screwdriver", "flush cutters", "soldering iron", "tweezers", "superglue"],
    tip: "The full list, with optional parts (LiPo, mesh, sleeve, spring), is in the BOM.",
    link: ["Bill of materials", "https://github.com/alexnodeland/lynx/blob/main/hardware/production/BOM.md"],
  },
  {
    id: "print",
    chapter: "Prepare",
    title: "Print the eight parts",
    time: "≈ 9 h printing",
    show: "printed",
    body: `Print the three test coupons first (test-boss, test-lidcorner, test-usbend): they check
      the screw bosses, lid fit and USB-C opening in ten minutes. Then the parts, at 0.2 mm layers and 3 walls.`,
    list: [
      "Base: off-white, open side down, no supports",
      "Lid: frosted natural PETG, top face down, supports from the plate only",
      "Yoke: charcoal, plate down, 4 walls / 40 % infill (it carries the camera)",
      "Pan base, pan cap, servo shroud, battery tray: charcoal, no supports",
      "Light pipe: clear PETG, 100 % infill, 0.12 mm layers",
    ],
    tip: "If the test prints are tight or loose, change FIT in the CAD script and re-export.",
    link: ["Download the STLs", "https://github.com/alexnodeland/lynx/tree/main/hardware/production"],
  },
  {
    id: "flash",
    chapter: "Prepare",
    title: "Flash the board first",
    time: "5 min",
    show: ["Jig_XIAO_Sense", "Jig_Heatsink"],
    body: `Install the firmware while the XIAO is still bare: one click from your browser, over USB-C.
      Doing it now means you never depend on the base socket passing data.`,
    list: [
      "Open the web flasher in Chrome or Edge and click Install",
      "Join the lynx-xxxx Wi-Fi hotspot from your phone and set up Wi-Fi",
      "Save the device token the setup page shows",
    ],
    tip: "No port in the list? Hold BOOT while plugging the board in.",
    link: ["Open the web flasher", "./flash.html"],
    action: "led",
  },
  {
    id: "base",
    chapter: "Build",
    title: "Base: magnet, socket, strain relief",
    time: "10 min",
    add: ["PanBase", "Jig_Magnet", "Jig_USB_PanelSocket", "Jig_SpringReliefs"],
    from: { PanBase: [0, 0, -30], Jig_Magnet: [0, 0, -40], Jig_USB_PanelSocket: [0, 30, 0], Jig_SpringReliefs: [0, 0, 30] },
    body: `Press the magnet into the underside of the pan base until it's flush: the crush ribs hold it,
      no glue. Screw the USB-C panel socket into the pod and seat the spring in its counterbore on top.`,
    need: [["1", "magnet"], ["1", "USB-C panel socket"], ["1", "spring"]],
    tip: "Glue would bond the magnet to whatever it sticks to. The ribs are enough.",
  },
  {
    id: "pan-servo",
    chapter: "Build",
    title: "Pan servo and cap",
    time: "10 min",
    add: ["Jig_Servo_Pan", "PanCap"],
    from: { Jig_Servo_Pan: [0, 0, 36], PanCap: [0, 0, 40] },
    body: `Drop the pan servo into the base, spline up, and fix it with two screws through its tabs.
      Slide the pan cap over the housing and drive two screws into its sides.`,
    need: [["1", "MG90S servo"], ["4", "M2 × 8 screws"]],
    tip: "Thread-forming screws cut their own thread: drive them slowly the first time.",
  },
  {
    id: "yoke",
    chapter: "Build",
    title: "Yoke onto the pan horn",
    time: "5 min",
    add: ["Jig_Horn_Pan", "Yoke"],
    from: { Jig_Horn_Pan: [0, 0, 20], Yoke: [0, 0, 36] },
    body: `Centre the pan servo first (power it from the flashed board and send "look 0 45",
      or use the dashboard's Rest pose). Then press the horn into the yoke pocket, push the yoke onto
      the spline facing straight ahead, and fix it with the horn screw from above.`,
    need: [["1", "servo horn + its screw"]],
    tip: "A horn fitted off-centre costs you range on one side. It's worth the minute.",
  },
  {
    id: "tilt-servo",
    chapter: "Build",
    title: "Tilt servo and finned shroud",
    time: "10 min",
    add: ["Jig_Servo_Tilt", "ServoShroud"],
    from: { Jig_Servo_Tilt: [30, 0, 0], ServoShroud: [46, 0, 0] },
    body: `Pass the tilt servo through the yoke's right upright from the outside. Slide the finned
      shroud over its body and fix both with two screws through the shroud and the servo tabs.`,
    need: [["1", "MG90S servo"], ["2", "M2 × 8 screws"]],
  },
  {
    id: "enclosure",
    chapter: "Build",
    title: "Battery, tray and board",
    time: "15 min",
    lift: true,
    add: ["Base", "Jig_VentMesh", "Jig_LiPo_401723", "BatteryTray", "Jig_Heatsink", "Jig_XIAO_Sense", "Jig_MicroSD",
      "Jig_FPC_Antenna", "Jig_Antenna_Coax"],
    from: { Base: [0, 0, -26], Jig_VentMesh: [0, 0, 20], Jig_LiPo_401723: [0, 0, 34], BatteryTray: [0, 0, 44],
      Jig_Heatsink: [0, 0, 52], Jig_XIAO_Sense: [0, 0, 60], Jig_MicroSD: [34, 0, 0], Jig_FPC_Antenna: [-30, 0, 20],
      Jig_Antenna_Coax: [-20, 0, 30] },
    body: `Glue the stainless mesh behind the vent slots. Lay the LiPo in the base (optional; it keeps
      the camera up when unplugged), click the tray in over it, and set the board with its heatsink
      onto the tray's posts between the L-fences. Stick the antenna to the far wall and plug in the coax.`,
    need: [["1", "XIAO + heatsink + thermal pad"], ["1", "LiPo 150 mAh (optional)"], ["6", "mesh pieces"]],
    frame: { pad: 2.4 },
    tip: "Solder the LiPo's leads to the XIAO's BAT pads, not the JST plug: there's no room for it.",
  },
  {
    id: "lid",
    chapter: "Build",
    title: "Light pipe and lid",
    time: "5 min",
    lift: true,
    add: ["LightPipePrint", "Lid"],
    from: { LightPipePrint: [0, 0, 30], Lid: [0, 0, 46] },
    body: `Press the clear light pipe into the lid so its foot sits over the board's LED. Close the
      frosted lid and fix it with two screws.`,
    need: [["2", "M2 × 8 screws"]],
    tip: "The LED's position on the board is an estimate. If the dot looks dim, trim the pipe's riser.",
  },
  {
    id: "mount",
    chapter: "Build",
    title: "Mount the camera in the yoke",
    time: "10 min",
    add: ["Jig_Horn_Tilt"],
    from: { Jig_Horn_Tilt: [16, 0, 0] },
    mount: true,
    focus: "all",
    frame: { pad: 1.5, az: -24 },
    body: `Trim the tilt horn to 16 mm and seat it in the enclosure's drive cheek. Centre the tilt servo
      (serial "look 0 0": lens straight up), then slide the enclosure between the uprights: horn onto the spline on the right, the last
      screw through the left upright as the pivot.`,
    need: [["1", "M2 × 8 screw (the pivot)"]],
    tip: "Centre is tilt 0 (lens straight up). The firmware's rest pose then leans it 45° forward.",
  },
  {
    id: "cable",
    chapter: "Wire",
    title: "Route the USB-C cable",
    time: "10 min",
    add: ["Jig_USB_Plug_RA"],
    from: { Jig_USB_Plug_RA: [26, 0, 0] },
    cable: true,
    focus: "all",
    frame: { pad: 1.5, az: 125, el: 20 },
    body: `Sleeve the right-angle cable, plug it into the board, and run it through the three C-clips:
      the strain clip, the upright clip, the pan-loop clip. Then down through the spring into the pod,
      onto the panel socket.`,
    tip: "Leave a soft loop at the pan clip: it's what lets the head turn ±90° without tugging.",
  },
  {
    id: "wire",
    chapter: "Wire",
    title: "Servo wiring",
    time: "15 min",
    focus: ["Jig_Servo_Pan", "Jig_Servo_Tilt", "PanBase"],
    frame: { pad: 1.8, az: 40 },
    body: `Signals: pan to D0, tilt to D1, each through a 220 Ω resistor. Both servos' 5 V and GND go
      to the XIAO's 5V and GND, with the 470 µF capacitor across them inside the pod (negative to GND).
      Use the small carrier board, or solder the harness.`,
    list: ["D0 → pan signal (220 Ω)", "D1 → tilt signal (220 Ω)", "5V / GND → both servos + 470 µF"],
    tip: "Servos only run on USB power. On battery the camera stays up but the head won't move.",
    link: ["Wiring and harness", "https://github.com/alexnodeland/lynx/tree/main/hardware/electronics"],
  },
  {
    id: "connect",
    chapter: "Use",
    title: "Plug in and connect",
    time: "5 min",
    plug: true,
    show: "all",
    frame: { pad: 1.7, el: 18, min: 0.3 },
    body: `Plug USB-C into the base. lynx boots into its rest pose and joins your Wi-Fi. Open
      lynx-xxxx.local for the live view, add it to Home Assistant (it's discovered by itself), or let an
      AI agent drive it over MCP.`,
    list: [
      "Dashboard: http://lynx-xxxx.local/",
      "Home Assistant: HACS custom repo → it appears under Discovered",
      "MCP: claude mcp add --transport http lynx http://lynx-xxxx.local/mcp",
    ],
    tip: "Lost the token? Hold BOOT for 3 seconds to reopen the setup hotspot.",
    link: ["Firmware, API and MCP", "https://github.com/alexnodeland/lynx/tree/main/firmware/lynx"],
    action: "look",
  },
];

export const PRINTED = ["Base", "Lid", "BatteryTray", "Yoke", "PanBase", "PanCap", "ServoShroud", "LightPipePrint"];
