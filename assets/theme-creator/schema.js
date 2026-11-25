/* RiptOPL Theme Creator: what the theme parser reads.
 *
 * Every key, element type, family and value below was taken from RiptOPL's own parser
 * (src/core/config.cpp, src/gfx/themes.cpp, themeelems.cpp, themeviews.cpp, grid.cpp, textures.cpp)
 * at rebuild/main fe08c0e, and from docs/THEME_ENGINE.md. check_schema.js, next to this file, re-checks the
 * names against a RiptOPL checkout (node check_schema.js <RiptOPL>). Nothing here talks to a server.
 */
(function (g) {
  "use strict";

  var SCREEN_W = 640, SCREEN_H = 480;
  var KEY_MAX = 47;      // CONFIG_KEY_NAME_LEN 48, so 47 characters fit
  var VALUE_MAX = 255;   // CONFIG_KEY_VALUE_LEN 256
  var TEX_MAX = 1024;    // textures.cpp: a picture wider or taller than 1024 is refused

  var VIEWS = ["list", "coverflow", "psbbn", "grid"];
  var VIEW_LABEL = { list: "List", coverflow: "Coverflow", psbbn: "PSBBN Flow", grid: "Grid" };

  // Families and their per-slot fallback chains (themes.cpp vcdMainChain etc.).
  var FAMILIES = {
    main:        { chain: ["main"], label: "Games list", page: "main" },
    appsMain:    { chain: ["appsMain", "main"], label: "Apps list", page: "main" },
    favsMain:    { chain: ["favsMain", "main"], label: "Favorites list", page: "main" },
    vcdMain:     { chain: ["vcdMain", "appsMain", "main"], label: "PS1 list", page: "main" },
    favsVcdMain: { chain: ["favsVcdMain", "vcdMain", "appsMain", "main"], label: "Favorites PS1 list (opt-in)", page: "main", optIn: true },
    favsAppsMain:{ chain: ["favsAppsMain", "favsMain", "main"], label: "Favorites apps list (opt-in)", page: "main", optIn: true },
    info:        { chain: ["info"], label: "Game info", page: "info" },
    appsInfo:    { chain: ["appsInfo", "info"], label: "App info", page: "info" },
    favsInfo:    { chain: ["favsInfo", "info"], label: "Favorites info", page: "info" },
    vcdInfo:     { chain: ["vcdInfo", "info"], label: "PS1 info", page: "info" },
    favsVcdInfo: { chain: ["favsVcdInfo", "vcdInfo", "info"], label: "Favorites PS1 info (opt-in)", page: "info", optIn: true },
    favsAppsInfo:{ chain: ["favsAppsInfo", "favsInfo", "info"], label: "Favorites apps info (opt-in)", page: "info", optIn: true }
  };
  var FAMILY_NAMES = Object.keys(FAMILIES);
  var BLOCK_RE = new RegExp("^(?:(" + VIEWS.join("|") + ")\\.)?(" + FAMILY_NAMES.join("|") + ")(\\d+)$");

  // Element types (themes.cpp elementsType) with the defaults initBasic is given for each.
  // x/y "MID" = centre, negative = from the right/bottom edge. w/h null = the picture's own size.
  var TYPES = {
    Background:    { x: 0, y: 0, aligned: 0, w: "INF", h: "INF", scaled: 0, image: true, desc: "The full-screen picture behind everything. Must be block 0. Without a picture it draws the plasma." },
    StaticImage:   { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, image: true, desc: "A fixed picture: a logo, a bezel, a panel." },
    StaticText:    { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, text: true, desc: "Fixed text from value=." },
    AttributeText: { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, text: true, item: true, desc: "A game's details: title, developer, genre, release, description, size." },
    GameCountText: { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, text: true, item: true, desc: "How many items the list holds." },
    GameImage:     { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, image: true, item: true, desc: "A game's artwork chosen by pattern= (cover, screenshot, logo, disc...)." },
    AttributeImage:{ x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, image: true, item: true, desc: "A badge picked by a game attribute, such as PS2_#System.png." },
    MenuIcon:      { x: "MID", y: 400, aligned: 1, w: null, h: null, scaled: 1, desc: "The icon of the device page you are on." },
    MenuText:      { x: "MID", y: 20, aligned: 1, w: 200, h: 20, scaled: 1, text: true, desc: "The name of the device page, with arrows." },
    ItemsList:     { x: 42, y: 42, aligned: 0, w: 400, h: 360, scaled: 1, text: true, item: true, desc: "The scrolling list of games. Keep one without devices= in every theme." },
    ItemIcon:      { x: 0, y: 0, aligned: 1, w: 64, h: 64, scaled: 1, image: true, item: true, desc: "The selected game's disc icon (ICO art)." },
    ItemCover:     { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, image: true, item: true, desc: "The selected game's cover, with an optional case overlay." },
    ItemText:      { x: 0, y: 0, aligned: 1, w: null, h: null, scaled: 1, text: true, item: true, desc: "The selected game's startup file name (SLUS_123.45)." },
    HintText:      { x: 16, y: -32, aligned: 0, w: 12, h: 20, scaled: 1, desc: "The button hints along the bottom of the list page." },
    InfoHintText:  { x: 16, y: -32, aligned: 0, w: 12, h: 20, scaled: 1, desc: "The button hints of the info page." },
    LoadingIcon:   { x: -40, y: -60, aligned: 1, w: null, h: null, scaled: 1, desc: "The animated loading spinner." },
    BdmIndex:      { x: "MID", y: 355, aligned: 1, w: null, h: null, scaled: 1, desc: "Which USB/BDM drive is shown, when there are several." },
    Coverflow:     { x: "MID", y: "MID", aligned: 1, w: 150, h: 210, scaled: 1, image: true, item: true, presenter: "coverflow", desc: "A carousel of 3 or 5 covers with a reflection." },
    PSBBNFlow:     { x: -186, y: "MID", aligned: 1, w: 204, h: 204, scaled: 1, image: true, item: true, presenter: "psbbn", desc: "PSBBN's Game Collection fan of square covers." },
    Grid:          { x: "MID", y: "MID", aligned: 1, w: 80, h: 112, scaled: 1, image: true, item: true, presenter: "grid", desc: "The page's covers in rows and columns." },
    SaveIcon:      { x: "MID", y: "MID", aligned: 1, w: 64, h: 64, scaled: 1, item: true, desc: "The game's spinning 3D save icon (only while 3D Save Icons is on)." }
  };
  var TYPE_NAMES = Object.keys(TYPES);

  // Global keys (top of the file). kind: color | int | bool | str | font | list | enum
  var GLOBALS = {
    bg_color:            { kind: "color", group: "Colours", label: "Background tint (plasma high end)", def: "#0C3AA8" },
    plasma_blend_color:  { kind: "color", group: "Colours", label: "Plasma low end", def: "#000000" },
    text_color:          { kind: "color", group: "Colours", label: "Text", def: "#47526C" },
    ui_text_color:       { kind: "color", group: "Colours", label: "Menu and hint text", def: "#4F5A74" },
    sel_text_color:      { kind: "color", group: "Colours", label: "Selected text", def: "#FFFFFF" },
    title_color:         { kind: "color", group: "Colours", label: "List titles", def: "" },
    default_font:        { kind: "font", group: "Fonts", label: "Main font file" },
    default_font_size:   { kind: "int", group: "Fonts", label: "Main font size (px)", min: 1, max: 96 },
    default_font_weight: { kind: "int", group: "Fonts", label: "Main font weight (100 to 900)", min: 100, max: 900 },
    use_default:         { kind: "bool", group: "Behaviour", label: "Use built-in pictures for anything the theme lacks", def: "1" },
    use_real_height:     { kind: "bool", group: "Behaviour", label: "Lay out against the real screen height", def: "0" },
    use_settings_bg:     { kind: "bool", group: "Behaviour", label: "Show settings_bg.png behind the settings menus", def: "0" },
    list_cfg_text:       { kind: "bool", group: "Behaviour", label: "Show the game CFG Title and Description on list pages", def: "1" },
    coverflow_cover_offset: { kind: "int", group: "Behaviour", label: "Coverflow horizontal nudge", min: -1024, max: 1024 },
    alt:                 { kind: "image", group: "Behaviour", label: "Alternate background picture" },
    hd_autofit:          { kind: "bool", group: "Behaviour", label: "Keep the 4:3 layout's shape in 16:9 automatically", def: "0" },
    views:               { kind: "list", group: "Views", label: "Views this theme offers (empty = all four)", values: VIEWS },
    default_view:        { kind: "enum", group: "Views", label: "View used until the user picks one", values: [""].concat(VIEWS) },
    glyph_set:           { kind: "enum", group: "Button hints", label: "Button pictures", values: ["", "theme", "builtin", "standard", "white"] },
    hints_hide:          { kind: "str", group: "Button hints", label: "Hints to leave off (menu, run, info, options, refresh, star, view)" },
    hints_order:         { kind: "str", group: "Button hints", label: "Hints to show first, in order" },
    hints_labels:        { kind: "bool", group: "Button hints", label: "Draw labels beside the buttons", def: "1" },
    hints_layout:        { kind: "enum", group: "Button hints", label: "Layout", values: ["", "row", "column"] },
    hints_spacing:       { kind: "int", group: "Button hints", label: "Column line pitch (12 to 96)", min: 12, max: 96 },
    badge_set:           { kind: "enum", group: "Button hints", label: "Badge set", values: ["", "ripps2"] }
  };
  for (var f = 1; f <= 15; f++) {
    GLOBALS["font" + f] = { kind: "font", group: "Fonts", label: "Font " + f + " file", extra: true };
    GLOBALS["font" + f + "_size"] = { kind: "int", group: "Fonts", label: "Font " + f + " size", extra: true };
    GLOBALS["font" + f + "_weight"] = { kind: "int", group: "Fonts", label: "Font " + f + " weight", extra: true };
  }
  // Keys a <view>. prefix may override (themeviews.cpp thmView* readers).
  var VIEW_GLOBALS = ["use_default", "use_real_height", "coverflow_cover_offset", "bg_color", "plasma_blend_color",
    "text_color", "ui_text_color", "title_color", "sel_text_color", "hd_autofit", "inherit"];

  // Element properties. "for" limits where the form shows them; the parser reads any key it knows.
  var PROPS = {
    type:      { kind: "type" },
    x:         { kind: "pos", label: "X", help: "Negative counts from the right edge. POS_MID is the centre." },
    y:         { kind: "pos", label: "Y", help: "Negative counts from the bottom edge. POS_MID is the centre." },
    width:     { kind: "dim", label: "Width", help: "DIM_INF is the full screen width." },
    height:    { kind: "dim", label: "Height", help: "DIM_INF is the full screen height." },
    aligned:   { kind: "enum", label: "Anchor", values: ["", "0", "1", "2"], names: { "0": "Top-left corner", "1": "Centre", "2": "Right edge, centred up and down" } },
    scaled:    { kind: "bool", label: "Keep proportions on 16:9 and PS2 pixels" },
    color:     { kind: "color", label: "Colour" },
    font:      { kind: "int", label: "Font slot (0 to 15)", min: 0, max: 15 },
    enabled:   { kind: "bool", label: "Shown", def: "1" },
    devices:   { kind: "str", label: "Only on these device pages", for: ["MenuIcon", "ItemsList", "HintText"], help: "usb, ilink, mx4sio, hdd_bd, hdd, eth, smb, mmce, udpbd, udpfs, app, fav, bdm" },
    default:   { kind: "image", label: "Picture", for: "image" },
    alt:       { kind: "image", label: "Picture with Alternate Background on", for: "image" },
    pattern:   { kind: "str", label: "Game art kind", for: ["Background", "GameImage", "ItemCover", "Coverflow", "PSBBNFlow", "Grid"], suggest: ["COV", "COV2", "BG", "SCR", "SCR2", "ICO", "LGO"] },
    count:     { kind: "int", label: "Art cache slots", for: ["GameImage", "ItemCover", "Coverflow", "PSBBNFlow", "Grid", "Background"] },
    overlay:   { kind: "image", label: "Frame picture over it", for: ["StaticImage", "GameImage", "ItemCover", "Coverflow", "PSBBNFlow", "Grid", "AttributeImage"] },
    overlay2:  { kind: "image", label: "Second frame layer", for: ["StaticImage", "GameImage", "ItemCover", "Coverflow", "PSBBNFlow", "Grid", "AttributeImage"] },
    overlay_ulx: { kind: "int", label: "Art top-left X inside the frame", for: "overlay" },
    overlay_uly: { kind: "int", label: "Art top-left Y", for: "overlay" },
    overlay_urx: { kind: "int", label: "Art top-right X", for: "overlay" },
    overlay_ury: { kind: "int", label: "Art top-right Y", for: "overlay" },
    overlay_llx: { kind: "int", label: "Art bottom-left X", for: "overlay" },
    overlay_lly: { kind: "int", label: "Art bottom-left Y", for: "overlay" },
    overlay_lrx: { kind: "int", label: "Art bottom-right X", for: "overlay" },
    overlay_lry: { kind: "int", label: "Art bottom-right Y", for: "overlay" },
    reflection:  { kind: "bool", label: "Mirrored reflection", for: ["Coverflow", "GameImage", "ItemCover", "StaticImage"] },
    reflection_offset: { kind: "int", label: "Reflection offset", for: ["Coverflow", "GameImage", "ItemCover", "StaticImage"] },
    value:     { kind: "str", label: "Text", for: ["StaticText"] },
    attribute: { kind: "attr", label: "Attribute", for: ["AttributeText", "AttributeImage"] },
    display:   { kind: "enum", label: "Show", for: ["AttributeText", "StaticText", "GameCountText"], values: ["", "0", "1", "2"], names: { "0": "Label and value", "1": "Only when there is a value", "2": "Value only" } },
    wrap:      { kind: "bool", label: "Wrap long text", for: "text" },
    title:     { kind: "str", label: "Label in place of the automatic one", for: ["AttributeText"] },
    show_run:  { kind: "bool", label: "Show the Run hint", for: ["InfoHintText"], def: "1" },
    decorator: { kind: "str", label: "Per-row icon art kind", for: ["ItemsList"], suggest: ["ICO", "LGO"] },
    hidden:    { kind: "bool", label: "Hide the rows (navigation only)", for: ["ItemsList"] },
    highlight: { kind: "enum", label: "Selected row", for: ["ItemsList"], values: ["", "bar", "line", "none"], names: { bar: "Bar", line: "Underline", none: "None" } },
    disc:      { kind: "bool", label: "Draw the disc art too", for: ["Grid", "Coverflow", "ItemCover"] },
    style:     { kind: "enum", label: "Style", for: ["MenuText"], values: ["", "psbbn", "psbbn_active", "psbbn_bar"], names: { psbbn: "PSBBN three rows", psbbn_active: "PSBBN active page", psbbn_bar: "PSBBN device bar" } },
    wsX:       { kind: "pos", label: "X on 16:9 (uOPL/wOPL spelling of x_hd)" },
    rows:      { kind: "int", label: "Rows (1 to 4)", for: ["Grid"], min: 1, max: 4 },
    columns:   { kind: "int", label: "Columns (1 to 8)", for: ["Grid"], min: 1, max: 8 },
    spacing:   { kind: "int", label: "Gap between covers", for: ["Grid"] },
    counter:   { kind: "bool", label: "Position counter", for: ["Grid"], def: "1" },
    title_y:   { kind: "int", label: "Title line Y", for: ["Grid"] },
    title_font:{ kind: "int", label: "Title line font slot", for: ["Grid"] },
    vignette:  { kind: "enum", label: "Vignette on other covers", for: ["Grid"], values: ["", "0", "1", "2"], names: { "0": "Off", "1": "Weak", "2": "Full" } },
    coverless: { kind: "enum", label: "Game with no cover", for: ["Grid"], values: ["", "default", "name", "serial"] },
    flip:      { kind: "bool", label: "Turn the case over with the right stick", for: ["Grid"], def: "1" },
    cell_width:{ kind: "int", label: "Cell width", for: ["Grid"] },
    cell_height:{ kind: "int", label: "Cell height", for: ["Grid"] }
  };
  // Grid reads title=0/1 and devices=0/1 (grid.cpp) under the same names other types use differently.
  var GRID_ONLY_NAMES = { title: { kind: "bool", label: "Selected title line under the grid" }, devices: { kind: "bool", label: "Device sidebar", def: "1" } };

  var ATTR_TEXT = ["#Name", "Title", "Developer", "Genre", "Release", "Description", "#Size", "#Startup", "#Format", "#Media", "#System"];
  var ATTR_IMAGE = ["#System", "#Media", "#Format", "#DiscType", "Vmode", "Aspect", "Scan", "Players", "Rating"];
  var DEVICE_NAMES = ["usb", "ilink", "mx4sio", "hdd_bd", "hdd", "eth", "smb", "mmce", "udpbd", "udpfs", "app", "fav", "bdm"];
  var HINT_NAMES = ["menu", "run", "info", "options", "refresh", "star", "view", "start", "select", "r3", "l3", "cross", "circle", "square", "triangle"];

  // textures.cpp internalDefault: names default=/overlay= may use with no file. A theme file of the same
  // name (<name>.png in the theme folder) replaces it.
  var BUILTIN = {
    "Covers and art": ["cover", "coverapp", "disc", "screen", "screens", "missing", "case", "apps_case", "case_overlay", "psbbn_bg", "bezel_top", "bezel_bottom", "settings_bg"],
    "Device icons": ["usb", "usb_bd", "ilk_bd", "m4s_bd", "hdd_bd", "mmce", "hdd", "eth", "udp_bd", "udp_fs", "app", "fav"],
    "Buttons": ["cross", "circle", "triangle", "square", "left", "right", "select", "start", "L1", "R1", "L3", "R3", "fav_mark"],
    "Loading and drives": ["load0", "load1", "load2", "load3", "load4", "load5", "load6", "load7", "Index_0", "Index_1", "Index_2", "Index_3", "Index_4"],
    "Badges": ["PS1", "PS2", "ELF", "HDL", "ISO", "VCD", "ZSO", "UL", "CUE", "EXE", "BIN", "APP", "CD", "DVD", "Aspect_s", "Aspect_w", "Aspect_w1", "Aspect_w2",
      "Rating_0", "Rating_1", "Rating_2", "Rating_3", "Rating_4", "Rating_5", "no_Rating", "Players_1", "Players_2", "Players_3", "Players_4",
      "Scan_240p", "Scan_240p1", "Scan_480i", "Scan_480p", "Scan_480p1", "Scan_480p2", "Scan_480p3", "Scan_480p4", "Scan_480p5", "Scan_576i", "Scan_576p",
      "Scan_720p", "Scan_1080i", "Scan_1080i2", "Scan_1080p", "Vmode_multi", "Vmode_ntsc", "Vmode_pal", "no_Device"],
    "Boot logo": ["logo"]
  };
  // Built-in names whose embedded picture is another file (textures.cpp internalDefault).
  var BUILTIN_FILE = { APP: "APPS", apps_case: "case", no_Rating: "Rating_0" };
  var BUILTIN_ALL = {};
  Object.keys(BUILTIN).forEach(function (k) { BUILTIN[k].forEach(function (n) { BUILTIN_ALL[n] = 1; }); });
  // These have no embedded picture in the ELF; a theme must ship them to show anything.
  var BUILTIN_EMPTY = { case_overlay: 1, settings_bg: 1, incebtion: 1, ip: 1, no_Device: 1 };

  g.RTC_SCHEMA = {
    SCREEN_W: SCREEN_W, SCREEN_H: SCREEN_H, KEY_MAX: KEY_MAX, VALUE_MAX: VALUE_MAX, TEX_MAX: TEX_MAX,
    VIEWS: VIEWS, VIEW_LABEL: VIEW_LABEL, FAMILIES: FAMILIES, FAMILY_NAMES: FAMILY_NAMES, BLOCK_RE: BLOCK_RE,
    TYPES: TYPES, TYPE_NAMES: TYPE_NAMES, GLOBALS: GLOBALS, VIEW_GLOBALS: VIEW_GLOBALS, PROPS: PROPS,
    GRID_ONLY_NAMES: GRID_ONLY_NAMES, ATTR_TEXT: ATTR_TEXT, ATTR_IMAGE: ATTR_IMAGE, DEVICE_NAMES: DEVICE_NAMES,
    HINT_NAMES: HINT_NAMES, BUILTIN: BUILTIN, BUILTIN_FILE: BUILTIN_FILE, BUILTIN_ALL: BUILTIN_ALL, BUILTIN_EMPTY: BUILTIN_EMPTY
  };
})(typeof window !== "undefined" ? window : globalThis);
