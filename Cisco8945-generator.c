/*
 * ======================================================================================
 * CISCO 8945 CONFIGURATION GENERATOR
 * ======================================================================================
 * By Robert Rogers @robsyoutube
 *
 * PURPOSE:
 * This program creates the "SEP<MAC>.cnf.xml" configuration file required to
 * provision a Cisco 8945 IP Phone. It is designed to be:
 *  1. Complete: Covering SIP, Network, Video, and VLAN settings.
 *  2. Novice-Friendly: Providing clear, on-screen help for every setting.
 *  3. Robust: Preventing common errors like invalid MAC addresses.
 *
 * COMPILATION:
 *  Linux/Mac: gcc -o cisco_gen CISCO8945-phone.c -lncurses
 *
 * ======================================================================================
 */

#ifdef _WIN32
#include <curses.h>
#include <windows.h>
#else
#include <ncurses.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <stdio.h>

#include <string.h>

// Maximum number of configuration fields we can handle.
// We increased this to 500 to handle the large number of new options.
#define MAX_FIELDS 500

/*
 * ENUM: FieldType
 * Defines how a field behaves in the UI.
 *  - FT_MANDATORY: Must be filled in (Highlighted Red).
 *  - FT_OPTIONAL:  Can be left blank (Highlighted Cyan).
 *  - FT_HEADER:    A section title (White/Bold).
 */
typedef enum { FT_MANDATORY, FT_OPTIONAL, FT_HEADER } FieldType;

/*
 * STRUCT: Field
 * Represents a single configuration setting.
 *  - label: The text shown to the user (e.g., "Primary IP").
 *  - value: The current value entered by the user.
 *  - xml:   The XML tag name this maps to (e.g., "processNodeName").
 *  - help:  The explanation text shown at the bottom of the screen.
 *  - options/xml_values: Arrays for Dropdown choices (User Label vs XML Value).
 */
typedef struct {
  char label[64];
  char value[128];
  char xml[64];
  char help[512];
  FieldType type;
  char options[100][64];    // Increased for FULL timezone list
  char xml_values[100][64]; // Increased for FULL timezone list
  int opt_count;
  int opt_sel;
  int hidden; // 1 = Hidden, 0 = Visible
} Field;

Field fields[MAX_FIELDS];
int total_fields = 0;
int current = 1;

// --- DYNAMIC LOOKUP HELPERS ---
Field *get_field(const char *xml_tag) {
  for (int i = 0; i < total_fields; i++) {
    if (strcmp(fields[i].xml, xml_tag) == 0)
      return &fields[i];
  }
  return NULL;
}
char *val(const char *tag) {
  Field *f = get_field(tag);
  return f ? f->value : "";
}
char *opt_val(const char *tag) {
  Field *f = get_field(tag);
  return f ? f->xml_values[f->opt_sel] : "";
}
// ------------------------------------------------------------------------

/*
 * FUNCTION: sanitize_mac
 * input:  User entered string (src)
 * output: Modifies src in place
 * purpose: Removes colons, dashes, and everything except 0-9, A-F.
 *          Ensures the MAC address is the correct format for the filename.
 */
void sanitize_mac(char *src) {
  char clean[128] = {0};
  int j = 0;
  for (int i = 0; src[i]; i++)
    if (isxdigit(src[i]))
      clean[j++] = toupper(src[i]);
  clean[12] = '\0'; // Truncate to 12 chars standard MAC length
  strcpy(src, clean);
}

/*
 * FUNCTION: update_visibility
 * purpose:  This is the "Logic Brain" of the UI. It hides/shows fields
 *           based on other selections.
 *           Example: If "Line 1" is Disabled, hide the Extension/Password/Label
 * fields.
 */
void update_visibility() {
  for (int i = 0; i < total_fields; i++) {
    // 1. Line Key Logic
    // If the user selects "Disabled" (option 0) for a button, hide the
    // sub-settings.
    if (strstr(fields[i].label, "Key Function")) {
      int type = fields[i].opt_sel;
      fields[i + 1].hidden = (type == 0); // Number/Extension
      fields[i + 2].hidden = (type == 0); // Label/Display Name
      fields[i + 3].hidden = (type != 1); // Auth ID
      fields[i + 4].hidden = (type != 1); // Password
      fields[i + 5].hidden = (type != 1); // Auto Answer
      fields[i + 6].hidden = (type != 1); // Forward All
      fields[i + 7].hidden = (type != 1); // Pickup Group
      fields[i + 8].hidden = (type != 1); // Voicemail
    }

    // 2. SNMP Logic
    // Only show Community String if SNMP is Enabled.
    if (strcmp(fields[i].xml, "snmpCommunity") == 0)
      fields[i].hidden = (fields[i - 1].opt_sel == 0);

    // 3. NAT Logic
    // Only show Public IP field if NAT is Enabled.
    if (strcmp(fields[i].xml, "natAddress") == 0)
      fields[i].hidden = (fields[i - 1].opt_sel == 0);

    // 4. PC Port VLAN Logic
    // Only show the specific VLAN ID field if the user selected Mode 2
    // ("Specific").
    if (strcmp(fields[i].xml, "pcPortVlanId") == 0)
      fields[i].hidden = (fields[i - 1].opt_sel != 2);
  }
}

// --- FIELD CREATION HELPERS
// -----------------------------------------------------------

void add_field(const char *lbl, const char *xml, FieldType t, const char *hlp,
               int hide) {
  Field *f = &fields[total_fields++];
  strncpy(f->label, lbl, 63);
  strncpy(f->xml, xml ? xml : "", 63); // Some headers have no XML mapping
  f->value[0] = '\0';
  strncpy(f->help, hlp, 511);
  f->type = t;
  f->opt_count = 0;
  f->hidden = hide;
}

void add_dropdown(const char *lbl, const char *xml, const char *hlp,
                  const char *opts[], const char *vals[], int count, int def,
                  int hide) {
  add_field(lbl, xml, FT_OPTIONAL, hlp, hide);
  Field *f = &fields[total_fields - 1];
  f->opt_count = count;
  f->opt_sel = def; // Default selection index
  for (int i = 0; i < count; i++) {
    strncpy(f->options[i], opts[i], 63);
    strncpy(f->xml_values[i], vals[i], 63);
  }
  strcpy(f->value, opts[def]);
}

// --- POPUP MENU SYSTEM
// --------------------------------------------------------------
/*
 * FUNCTION: show_popup
 * purpose:  Draws a floating window on top of the form for Dropdown selections.
 *           Optimized to handle LONG lists (scrolling) if they don't fit on
 * screen.
 */
int show_popup(Field *f) {
  int h, w;
  getmaxyx(stdscr, h, w);

  // Calculate Window Size (Cap height to fit screen)
  int list_len = f->opt_count;
  int max_h = h - 6; // Leave some margin
  int box_h = list_len + 2;
  if (box_h > max_h)
    box_h = max_h;

  int box_w = 48;

  // Center the popup
  int start_y = (h / 2) - (box_h / 2);
  int start_x = (w / 2) - (box_w / 2);

  WINDOW *pw = newwin(box_h, box_w, start_y, start_x);
  if (!pw)
    return 0; // Failsafe

  keypad(pw, TRUE);

  int sel = f->opt_sel;
  int scroll_top = 0;

  // Initial Scroll Position
  int visible_items = box_h - 2;
  if (sel >= visible_items)
    scroll_top = sel - visible_items + 1;

  while (1) {
    // Scroll Logic check
    if (sel < scroll_top)
      scroll_top = sel;
    if (sel >= scroll_top + visible_items)
      scroll_top = sel - visible_items + 1;

    // Draw Box & Title
    werase(pw);
    box(pw, 0, 0);
    // Optional: Scroll indicators
    if (scroll_top > 0)
      mvwaddch(pw, 0, box_w - 2, '^');
    if (scroll_top + visible_items < list_len)
      mvwaddch(pw, box_h - 1, box_w - 2, 'v');

    for (int i = 0; i < visible_items; i++) {
      int idx = scroll_top + i;
      if (idx >= list_len)
        break;

      if (idx == sel)
        wattron(pw, A_REVERSE);

      // Truncate to 44 chars to prevent wrapping
      mvwprintw(pw, i + 1, 2, "%-44.44s", f->options[idx]);

      if (idx == sel)
        wattroff(pw, A_REVERSE);
    }
    wrefresh(pw);

    int ch = wgetch(pw);
    if (ch == KEY_UP && sel > 0)
      sel--;
    else if (ch == KEY_DOWN && sel < f->opt_count - 1)
      sel++;
    else if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
      f->opt_sel = sel;
      strcpy(f->value, f->options[sel]);
      update_visibility();
      delwin(pw);
      touchwin(stdscr); // Redraw underlying window
      return 1;
    } else if (ch == 27) { // Escape
      delwin(pw);
      touchwin(stdscr);
      return 0;
    }
  }
}

/*
 * FUNCTION: show_text_input
 * purpose:  Opens a centered popup box for typing text.
 *           Replaces the inline bottom-screen entry for a modern look.
 *           NOW INCLUDES: Help Text and Examples inside the box!
 */
void show_text_input(Field *f) {
  int h, w;
  getmaxyx(stdscr, h, w);
  int box_h = 14; // Taller for Help Text
  int box_w = 64; // Wider for longer descriptions

  if (box_w > w)
    box_w = w - 4;
  if (box_h > h)
    box_h = h - 2;

  // Center
  int start_y = (h / 2) - (box_h / 2);
  int start_x = (w / 2) - (box_w / 2);

  WINDOW *win = newwin(box_h, box_w, start_y, start_x);
  keypad(win, TRUE);
  box(win, 0, 0);

  // Title
  wattron(win, A_BOLD | COLOR_PAIR(6));
  mvwprintw(win, 0, 2, "[ %s ]", f->label);
  wattroff(win, A_BOLD | COLOR_PAIR(6));

  // Instruction
  mvwprintw(win, 2, 2, "Enter Value:");

  // Input Area (Lines 3-5 reserved for input/validation)
  // Draw an underline
  mvwhline(win, 4, 2, ACS_HLINE, box_w - 4);

  // Help Text (Lines 6+)
  wattron(win, COLOR_PAIR(6)); // Green Text for Help
  mvwprintw(win, 6, 2, "HELP / EXAMPLES:");

  // Wrap Help Text
  int help_w = box_w - 4;
  char help_buf[512];
  strncpy(help_buf, f->help, 511);

  int curr_y = 7;
  char *p = help_buf;
  while (*p && curr_y < box_h - 1) {
    char line[128];
    int line_len = 0;
    int temp_idx = 0;
    while (temp_idx < help_w && p[temp_idx] != '\0') {
      line[temp_idx] = p[temp_idx];
      temp_idx++;
    }
    line[temp_idx] = '\0';
    line_len = temp_idx;

    // Find last space to break cleanly
    if (line_len == help_w && p[line_len] != '\0') {
      char *sp = strrchr(line, ' ');
      if (sp) {
        *sp = '\0';
        line_len = sp - line;
      }
    }

    mvwprintw(win, curr_y++, 2, "%s", line);
    p += line_len;
    while (*p == ' ')
      p++; // Skip space
  }
  wattroff(win, COLOR_PAIR(6));

  // Perform Input
  echo();
  curs_set(1);
  wattron(win, A_BOLD | COLOR_PAIR(1)); // Cyan Text
  mvwprintw(win, 3, 2, "%-58s", "");    // Clear line
  mvwgetnstr(win, 3, 2, f->value, 127);
  wattroff(win, A_BOLD | COLOR_PAIR(1));
  noecho();
  curs_set(0);

  // Auto-Sanitize
  if (strstr(f->label, "MAC"))
    sanitize_mac(f->value);

  delwin(win);
  touchwin(stdscr);
}

/*
 * FUNCTION: init_fields
 * purpose:  Defines the entire form structure.
 *           This is where we list every single configurable option.
 */
void init_fields() {
  total_fields = 0;

  // --- CONSTANT DATA ARRAYS ---
  const char *dis_en[] = {"Disabled", "Enabled"}, *dis_en_val[] = {"0", "1"};
  const char *no_yes[] = {"No", "Yes"}, *no_yes_val[] = {"false", "true"};
  const char *transports[] = {"UDP", "TCP", "TLS"},
             *trans_vals[] = {"1", "2", "3"};

  const char *codecs[] = {"G.711u (Standard US)", "G.711a (Standard EU)",
                          "G.722 (HD Audio)", "G.729 (Compressed)"},
             *codec_vals[] = {"PCMU", "PCMA", "G722", "G729"};

  const char *date_fmts[] = {"M/D/Y", "D/M/Y", "Y/M/D"},
             *date_vals[] = {"M/D/Y", "D/M/Y", "Y/M/D"};
  const char *time_fmts[] = {"12 Hour", "24 Hour"}, *time_vals[] = {"12", "24"};

  const char *bitrates[] = {"384k", "768k", "1.5M", "2.5M", "4M"},
             *bit_vals[] = {"384", "768", "1500", "2500", "4000"};

  // Standard Cisco Timezones (70+ Zones)
  const char *tz_names[] = {
      "Dateline Standard Time (GMT-12)",
      "Samoa Standard Time (GMT-11)",
      "Hawaiian Standard Time (GMT-10)",
      "Alaskan Standard Time (GMT-9)",
      "Pacific Standard/Daylight Time (GMT-8)",
      "Mountain Standard/Daylight Time (GMT-7)",
      "US Mountain Standard Time (GMT-7)",
      "Central Standard/Daylight Time (GMT-6)",
      "Mexico Standard/Daylight Time (GMT-6)",
      "Canada Central Standard Time (GMT-6)",
      "SA Pacific Standard Time (GMT-5)",
      "Eastern Standard/Daylight Time (GMT-5)",
      "US Eastern Standard Time (GMT-5)",
      "Atlantic Standard Time (GMT-4)",
      "SA Western Standard Time (GMT-4)",
      "Newfoundland Standard Time (GMT-3.5)",
      "E. South America Standard Time (GMT-3)",
      "SA Eastern Standard Time (GMT-3)",
      "Mid-Atlantic Standard Time (GMT-2)",
      "Azores Standard Time (GMT-1)",
      "GMT Standard/Daylight Time (GMT)",
      "Greenwich Standard Time (GMT)",
      "W. Europe Standard/Daylight Time (GMT+1)",
      "GTB Standard/Daylight Time (GMT+2)",
      "Egypt Standard/Daylight Time (GMT+2)",
      "E. Europe Standard/Daylight Time (GMT+2)",
      "Romance Standard/Daylight Time (GMT+2)",
      "Russian Standard Time (GMT+3)",
      "Near East Standard/Daylight Time (GMT+3)",
      "Iran Standard Time (GMT+3.5)",
      "Arabian Standard Time (GMT+4)",
      "Caucasus Standard/Daylight Time (GMT+4)",
      "Transitional Islamic State of Afghanistan Standard Time (GMT+4.5)",
      "Ekaterinburg Standard Time (GMT+5)",
      "West Asia Standard Time (GMT+5)",
      "India Standard Time (GMT+5.5)",
      "Nepal Standard Time (GMT+5.75)",
      "Central Asia Standard Time (GMT+6)",
      "Sri Lanka Standard Time (GMT+6)",
      "N. Central Asia Standard Time (GMT+6)",
      "Myanmar Standard Time (GMT+6.5)",
      "SE Asia Standard Time (GMT+7)",
      "North Asia Standard Time (GMT+7)",
      "China Standard/Daylight Time (GMT+8)",
      "Singapore Standard Time (GMT+8)",
      "Taipei Standard Time (GMT+8)",
      "W. Australia Standard Time (GMT+8)",
      "North Asia East Standard Time (GMT+8)",
      "Korea Standard Time (GMT+9)",
      "Tokyo Standard Time (GMT+9)",
      "Yakutsk Standard Time (GMT+9)",
      "Aus Central Standard Time (GMT+9.5)",
      "Cen. Australia Standard/Daylight Time (GMT+9.5)",
      "Aus Eastern Standard/Daylight Time (GMT+10)",
      "E. Australia Standard Time (GMT+10)",
      "Vladivostok Standard Time (GMT+10)",
      "Tasmania Standard/Daylight Time (GMT+10)",
      "Central Pacific Standard Time (GMT+11)",
      "New Zealand Standard/Daylight Time (GMT+12)",
      "Fiji Standard Time"};

  const char *tz_vals[] = {
      "Dateline Standard Time",
      "Samoa Standard Time",
      "Hawaiian Standard Time",
      "Alaskan Standard Time",
      "Pacific Standard/Daylight Time",
      "Mountain Standard/Daylight Time",
      "US Mountain Standard Time",
      "Central Standard/Daylight Time",
      "Mexico Standard/Daylight Time",
      "Canada Central Standard Time",
      "SA Pacific Standard Time",
      "Eastern Standard/Daylight Time",
      "US Eastern Standard Time",
      "Atlantic Standard Time",
      "SA Western Standard Time",
      "Newfoundland Standard Time",
      "E. South America Standard Time",
      "SA Eastern Standard Time",
      "Mid-Atlantic Standard Time",
      "Azores Standard Time",
      "GMT Standard/Daylight Time",
      "Greenwich Standard Time",
      "W. Europe Standard/Daylight Time",
      "GTB Standard/Daylight Time",
      "Egypt Standard/Daylight Time",
      "E. Europe Standard/Daylight Time",
      "Romance Standard/Daylight Time",
      "Russian Standard Time",
      "Near East Standard/Daylight Time",
      "Iran Standard Time",
      "Arabian Standard Time",
      "Caucasus Standard/Daylight Time",
      "Transitional Islamic State of Afghanistan Standard Time",
      "Ekaterinburg Standard Time",
      "West Asia Standard Time",
      "India Standard Time",
      "Nepal Standard Time",
      "Central Asia Standard Time",
      "Sri Lanka Standard Time",
      "N. Central Asia Standard Time",
      "Myanmar Standard Time",
      "SE Asia Standard Time",
      "North Asia Standard Time",
      "China Standard/Daylight Time",
      "Singapore Standard Time",
      "Taipei Standard Time",
      "W. Australia Standard Time",
      "North Asia East Standard Time",
      "Korea Standard Time",
      "Tokyo Standard Time",
      "Yakutsk Standard Time",
      "Aus Central Standard Time",
      "Cen. Australia Standard/Daylight Time",
      "Aus Eastern Standard/Daylight Time",
      "E. Australia Standard Time",
      "Vladivostok Standard Time",
      "Tasmania Standard/Daylight Time",
      "Central Pacific Standard Time",
      "New Zealand Standard/Daylight Time",
      "Fiji Standard Time"};

  const char *btn_types[] = {"Disabled", "Line", "SpeedDial", "BLF"},
             *btn_vals[] = {"0", "1", "2", "3"};

  const char *locales[] = {"US (English)", "UK (English)", "France (French)",
                           "Germany (German)", "Spain (Spanish)"},
             *locale_vals[] = {"United_States", "United_Kingdom", "France",
                               "Germany", "Spain"};

  const char *net_locales[] = {"United States", "United Kingdom", "France",
                               "Germany", "Spain"},
             *net_locale_vals[] = {"United_States", "United_Kingdom", "France",
                                   "Germany", "Spain"};

  const char *bt_profiles[] = {"Handsfree Only", "Headset Only", "Both"},
             *bt_vals[] = {"Handsfree", "Headset", "Handsfree,Headset"};

  const char *dnd_alerts[] = {"None", "Flash Screen", "Beep", "Flash & Beep"},
             *dnd_vals[] = {"0", "5", "1", "2"};

  const char *pc_vlan_modes[] = {"Native / Untagged", "Tag with Voice VLAN",
                                 "Tag with Specific VLAN"},
             *pc_vlan_vals[] = {"0", "1", "2"};

  // === SECTION 1: IDENTITY & NETWORK ===
  add_field("=== IDENTITY & NETWORK ===", NULL, FT_HEADER,
            "Core System Settings", 0);
  add_field("MAC Address", "device", FT_MANDATORY,
            "REQUIRED: The unique 12-char ID on the back of the phone.", 0);
  add_field("Phone Label", "deviceLabel", FT_OPTIONAL,
            "Custom text shown in the top status bar (e.g. 'Reception').", 0);

  add_field(
      "Primary PBX IP", "processNodeName1", FT_MANDATORY,
      "REQUIRED: IP Address of your SIP Server / PBX (e.g. 192.168.1.10).", 0);
  add_field("Secondary PBX", "processNodeName2", FT_OPTIONAL,
            "Backup Server IP (e.g. 192.168.1.11). Leave blank if none.", 0);
  add_field("Tertiary PBX", "processNodeName3", FT_OPTIONAL,
            "Second Backup Server IP. Leave blank if none.", 0);

  add_dropdown(
      "Transport", "transportLayerProtocol",
      "Network Protocol. UDP (Standard) is faster with lower overhead. Use "
      "TCP/TLS only if your provider requires reliable or encrypted signaling.",
      transports, trans_vals, 3, 0, 0);
  add_field(
      "Firmware Load", "loadInformation", FT_OPTIONAL,
      "Specific firmware version to load (e.g. sip8941_45.9-4-2-13). Leave "
      "blank to use the default load defined in the TFTP server config.",
      0);
  add_field("SIP Port", "voipControlPort", FT_OPTIONAL,
            "Port for SIP Signaling. Default is 5060. changing this may "
            "require firewall adjustments.",
            0);

  // === SECTION 2: VLAN & ETHERNET ===
  add_field("=== ETHERNET & VLAN ===", NULL, FT_HEADER,
            "Network Layer 2 Settings", 0);

  add_field(
      "Voice VLAN ID", "adminVlanId", FT_OPTIONAL,
      "VLAN ID for Voice traffic. Leave blank if Network Port is untagged.", 0);

  // Enhanced PC Port Logic
  add_dropdown(
      "PC Port VLAN Mode", "pcVoiceVlanAccess",
      "Determines which VLAN the computer connected to the phone will use.",
      pc_vlan_modes, pc_vlan_vals, 3, 0, 0);
  add_field("PC VLAN ID", "pcPortVlanId", FT_OPTIONAL,
            "Enter the VLAN ID for the computer (Data VLAN).",
            1); // Hidden by default

  add_dropdown(
      "Span to PC", "spanToPCPort",
      "Advanced: Copies all phone audio/traffic to the PC port. Used for "
      "Wireshark/Packet Capture. WARNING: Can reduce network performance.",
      dis_en, dis_en_val, 2, 0, 0);
  add_dropdown(
      "Gratuitous ARP", "gratuitousARP",
      "Send ARP updates on boot. Critical for scenarios where the Router might "
      "not know where the phone is (e.g. redundant links). (Rec: Enabled)",
      dis_en, dis_en_val, 2, 1, 0);
  add_field("MTU Size", "mtu", FT_OPTIONAL,
            "Max Transmission Unit. 1500 is Ethernet Standard. Use 1300-1400 "
            "for VPNs to prevent packet fragmentation and dropped calls.",
            0);

  // === SECTION 3: SECURITY & ACCESS ===
  add_field("=== SECURITY & ACCESS ===", NULL, FT_HEADER,
            "Device Access Control", 0);
  add_dropdown(
      "Settings Lock", "settingsAccess",
      "Locks the 'Settings' menu on the phone screen to prevent changes.",
      dis_en, dis_en_val, 2, 1, 0);
  add_dropdown("Web Access", "webAccess",
               "Enables the phone's web page for viewing/changing settings.",
               dis_en, dis_en_val, 2, 1, 0);
  add_dropdown("SSH Access", "sshAccess",
               "Enables SSH for advanced remote administration.", dis_en,
               dis_en_val, 2, 0, 0);
  add_field("SSH Username", "sshUserId", FT_OPTIONAL, "Username for SSH login.",
            0);
  add_field("SSH Password", "sshPassword", FT_OPTIONAL,
            "Password for SSH login.", 0);
  add_field("Admin Password", "adminPassword", FT_OPTIONAL,
            "Password to unlock the Settings menu or Web Interface.", 0);
  add_dropdown("PC Port", "pcPort", "Enable/Disable the PC Ethernet port.",
               dis_en, dis_en_val, 2, 1, 0);

  // === SECTION 4: HARDWARE & BLUETOOTH ===
  add_field("=== HARDWARE ===", NULL, FT_HEADER, "Physical Peripherals", 0);
  add_dropdown("Bluetooth", "bluetooth", "Enable Bluetooth Radio.", dis_en,
               dis_en_val, 2, 1, 0);
  add_dropdown("BT Profiles", "bluetoothProfile",
               "Allowed BT Profiles (Handsfree/Headset).", bt_profiles, bt_vals,
               3, 2, 0);

  // === SECTION 5: AUDIO & VIDEO ===
  add_field("=== AUDIO & VIDEO ===", NULL, FT_HEADER, "Codecs and Call Quality",
            0);
  add_dropdown("Preferred Codec", "preferredCodec",
               "Audio quality. G.711 is standard. G.729 is compressed.", codecs,
               codec_vals, 4, 0, 0);
  add_dropdown("Advertise G.722", "advertiseG722Codec",
               "Advertise G.722 support for High Definition calls.", dis_en,
               dis_en_val, 2, 1, 0);
  add_field(
      "Audio DSCP", "dscpForAudio", FT_OPTIONAL,
      "QoS Packet Tagging. 184 (EF - Expedited Forwarding) is the industry "
      "standard for Voice. Ensure your Switch/Router respects this tag.",
      0);
  add_field("RTP Min Port", "startMediaPort", FT_OPTIONAL,
            "Start of UDP Port range for Audio/Video. Default 16384. Ensure "
            "your Firewall allows this range inbound/outbound.",
            0);
  add_field("RTP Max Port", "stopMediaPort", FT_OPTIONAL,
            "End of UDP Port range for Audio/Video. Default 32766. Range must "
            "be large enough to handle concurrent calls.",
            0);

  add_dropdown("Video Enable", "videoCapability",
               "Enable the built-in camera for video calls. Requires a PBX "
               "that supports Video (H.264).",
               no_yes, no_yes_val, 2, 1, 0);
  add_dropdown("Start Video on Answer", "autoTransmitVideo",
               "Control if video starts automatically when you answer. 'No' "
               "provides privacy (Audio only) until you press the Video "
               "button. 'Yes' sends video immediately upon answering.",
               no_yes, no_yes_val, 2, 0, 0);
  add_dropdown("Video Quality", "videoBitRate",
               "Max bandwidth/quality for Video. Select based on your upload "
               "speed. 1.5M+ recommended for HD 720p.",
               bitrates, bit_vals, 5, 2, 0);
  add_field("Video DSCP", "dscpForVideo", FT_OPTIONAL,
            "QoS Tag for Video. 136 (AF41) is standard. Set lower priority "
            "than Audio to prioritize voice clarity.",
            0);
  add_dropdown("RTCP Stats", "rtcp",
               "Send detailed call quality reports (Jitter/Latency "
               "constraints) to the SIP Server.",
               dis_en, dis_en_val, 2, 1, 0);

  // === SECTION 6: FEATURES & DND ===
  add_field("=== FEATURES ===", NULL, FT_HEADER,
            "Do Not Disturb & User Features", 0);
  add_dropdown("Do Not Disturb", "dndControl",
               "Show the 'Do Not Disturb' button on the main screen.", dis_en,
               dis_en_val, 2, 1, 0);
  add_dropdown("DND Alert", "dndCallAlert",
               "How to notify you of incoming calls when DND is active.",
               dnd_alerts, dnd_vals, 4, 1, 0);
  add_field("DND Timer", "dndReminderTimer", FT_OPTIONAL,
            "Play a reminder tone every X minutes when DND is active.", 0);
  add_dropdown("NAT Enabled", "natEnabled",
               "Select 'Yes' if this phone is behind a home router/firewall. "
               "Essential for remote phones.",
               no_yes, no_yes_val, 2, 0, 0);
  add_field(
      "NAT Address", "natAddress", FT_OPTIONAL,
      "The Public IP Address of your internet connection. PRO TIP: If you have "
      "'One-Way Audio' (can't hear caller), setting this usually fixes it.",
      1);

  // === SECTION 7: SNMP & LOGGING ===
  add_field("=== MONITORING ===", NULL, FT_HEADER, "SNMP & Syslog", 0);
  add_dropdown("SNMP Enable", "snmpEnabled", "Enable Remote Monitoring.",
               dis_en, dis_en_val, 2, 0, 0);
  add_field("Community String", "snmpCommunity", FT_OPTIONAL,
            "SNMP Password (e.g. public).", 1);
  add_field("Syslog Server", "syslogAddr", FT_OPTIONAL,
            "IP Address for sending Debug Logs (e.g. 192.168.1.50).", 0);

  // === SECTION 8: REGION & TIME ===
  add_field("=== REGION & TIME ===", NULL, FT_HEADER, "Localization", 0);
  add_dropdown("Language", "userLocale", "Screen Language (Load from Server).",
               locales, locale_vals, 5, 0, 0);
  add_dropdown(
      "Dial Tones", "networkLocale",
      "Sets the specific frequencies for Dial Tone, Busy Signal, and Ringback. "
      "Must match your region (e.g. US vs UK) or calls may sound 'wrong'.",
      net_locales, net_locale_vals, 5, 0, 0);
  add_field("Dial Plan", "dialTemplate", FT_OPTIONAL,
            "Dialing Rules File (e.g. dialplan.xml).", 0);

  // Updated Timezone count (Exact count is 58)
  add_dropdown("Time Zone", "timeZone", "Local Time Zone.", tz_names, tz_vals,
               58, 4, 0); // Default to Pacific Time (4)

  add_field("NTP Server", "ntpServer", FT_OPTIONAL,
            "Time Server IP (e.g. pool.ntp.org or 4.2.2.2).", 0);
  add_dropdown("Date Format", "dateTemplate", "Display format.", date_fmts,
               date_vals, 3, 0, 0);
  add_dropdown("Time Format", "timeFormat", "Clock format.", time_fmts,
               time_vals, 2, 0, 0);

  // === SECTION 9: EXTERNAL URLS ===
  add_field("=== EXTERNAL URLS ===", NULL, FT_HEADER, "Integration Links", 0);
  add_field("Directory URL", "directoryURL", FT_OPTIONAL,
            "URL for the Corporate Phonebook.", 0);
  add_field("Services URL", "servicesURL", FT_OPTIONAL,
            "URL for the Services Menu.", 0);
  add_field("Auth URL", "authenticationURL", FT_OPTIONAL,
            "URL for validating Services.", 0);
  add_field("Info URL", "informationURL", FT_OPTIONAL,
            "URL for the '?' Help button.", 0);
  add_field("Softkey Template", "softKeyFile", FT_OPTIONAL,
            "XML file on TFTP server defining button layouts (e.g. "
            "softkeys.xml). Allows removing/reordering buttons like 'Redial'.",
            0);
  add_field("Idle/Saver URL", "idleURL", FT_OPTIONAL,
            "URL to an XML file for the screensaver. Activated when phone is "
            "idle for the Timeout duration.",
            0);
  add_field("Saver Timeout", "idleTimeout", FT_OPTIONAL,
            "Time in seconds before the screensaver starts (e.g. 300 = 5 "
            "Minutes). Set to 0 to disable.",
            0);
  add_field("Wallpaper URL", "backgroundImage", FT_OPTIONAL,
            "URL to a Background Image. SPECS: 640x480 resolution, PNG format, "
            "24-bit Color Depth. Other formats (JPG/BMP) will NOT work.",
            0);

  // === SECTION 10: SIP LINES (BUTTONS) ===
  for (int i = 1; i <= 4; i++) {
    char b[32];
    sprintf(b, "=== BUTTON %d ===", i);
    add_field(b, NULL, FT_HEADER, "Line Configuration", 0);
    sprintf(b, "Key Function");
    add_dropdown(
        b, "lineType",
        "Choose 'Line' for a standard extension, 'SpeedDial' for 1-touch "
        "calling, or 'BLF' to monitor if a colleague is on the phone.",
        btn_types, btn_vals, 4, (i == 1 ? 1 : 0), 0);

    add_field("Extension", "name", FT_OPTIONAL,
              "The phone number for this line (e.g. 1001).", 0);
    add_field("Label", "displayName", FT_OPTIONAL,
              "Label shown next to the button (e.g. 'Line 1').", 0);
    add_field("Auth ID", "authName", FT_OPTIONAL,
              "SIP Username (Often the same as Extension, but check provider).",
              0);
    add_field("SIP Password", "authPassword", FT_OPTIONAL,
              "SIP Password for this extension.", 0);

    add_dropdown(
        "Auto Answer", "autoAnswerEnabled",
        "If Enabled, the phone answers calls automatically on speaker.", dis_en,
        dis_en_val, 2, 0, 1);
    add_field("Forward All", "callForwardURI", FT_OPTIONAL,
              "Number to forward calls to unconditionally.", 1);
    add_field("Pickup Group", "callPickupGroupURI", FT_OPTIONAL,
              "Code to dial to pick up a call ringing in your group.", 1);
    add_field("Voicemail #", "voiceMailPilot", FT_OPTIONAL,
              "Number dialed when the 'Messages' button is pressed.", 1);
  }
  update_visibility();
}

/*
 * FUNCTION: save_xml
 * purpose:  Generates the actual SEP<MAC>.cnf.xml file.
 *           Uses the gathered `fields[]` data to write legal XML.
 */
void save_xml() {
  // Integrity Check: MAC Address length
  char *mac = val("device");
  if (strlen(mac) != 12) {
    mvprintw(LINES - 2, 2, "ERR: MAC MUST BE 12 CHARS!");
    getch();
    return;
  }

  char fn[64];
  sprintf(fn, "SEP%s.cnf.xml", mac);
  FILE *fp = fopen(fn, "w");
  if (!fp)
    return;

  // --- START XML GENERATION ---
  fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<device>\n");
  fprintf(fp, "  <deviceProtocol>SIP</deviceProtocol>\n");

  char *lbl = val("deviceLabel");
  if (lbl[0])
    fprintf(fp, "  <deviceLabel>%s</deviceLabel>\n", lbl);

  char *load = val("loadInformation");
  if (load[0])
    fprintf(fp, "  <loadInformation>%s</loadInformation>\n", load);

  // Call Manager Group
  fprintf(fp, "  <callManagerGroup>\n    <members>\n");
  char *port = val("voipControlPort");
  if (!port[0])
    port = "5060";

  // Primary
  fprintf(fp, "      <member priority=\"0\">\n        <callManager>\n");
  fprintf(fp,
          "          <ports>\n            "
          "<ethernetPhonePort>%s</ethernetPhonePort>\n          </ports>\n",
          port);
  fprintf(fp, "          <processNodeName>%s</processNodeName>\n",
          val("processNodeName1"));
  fprintf(fp, "        </callManager>\n      </member>\n");

  // Secondary
  char *s_pbx = val("processNodeName2");
  if (s_pbx[0]) {
    fprintf(fp, "      <member priority=\"1\">\n        <callManager>\n");
    fprintf(fp,
            "          <ports>\n            "
            "<ethernetPhonePort>%s</ethernetPhonePort>\n          </ports>\n",
            port);
    fprintf(fp, "          <processNodeName>%s</processNodeName>\n", s_pbx);
    fprintf(fp, "        </callManager>\n      </member>\n");
  }
  // Tertiary
  char *t_pbx = val("processNodeName3");
  if (t_pbx[0]) {
    fprintf(fp, "      <member priority=\"2\">\n        <callManager>\n");
    fprintf(fp,
            "          <ports>\n            "
            "<ethernetPhonePort>%s</ethernetPhonePort>\n          </ports>\n",
            port);
    fprintf(fp, "          <processNodeName>%s</processNodeName>\n", t_pbx);
    fprintf(fp, "        </callManager>\n      </member>\n");
  }
  fprintf(fp, "    </members>\n  </callManagerGroup>\n");

  // Date/Time
  fprintf(fp, "  <dateTimeSetting>\n");
  char *ntp = val("ntpServerAddr");
  if (ntp[0])
    fprintf(fp, "    <ntpServerAddr>%s</ntpServerAddr>\n", ntp);
  fprintf(fp, "    <timeZone>%s</timeZone>\n", opt_val("timeZone"));
  char *df = opt_val("dateTemplate");
  if (df[0])
    fprintf(fp, "    <dateTemplate>%s</dateTemplate>\n", df);
  char *tf = opt_val("timeFormat");
  if (tf[0])
    fprintf(fp, "    <timeFormat>%s</timeFormat>\n", tf);
  fprintf(fp, "  </dateTimeSetting>\n");

  // SIP Stack
  fprintf(fp, "  <sipStack>\n");
  fprintf(fp, "    <transportLayerProtocol>%s</transportLayerProtocol>\n",
          opt_val("transportLayerProtocol"));
  Field *nat = get_field("natEnabled"); // Need object for opt_sel check
  if (nat && nat->opt_sel == 1)
    fprintf(
        fp,
        "    <natEnabled>true</natEnabled>\n    <natAddress>%s</natAddress>\n",
        val("natAddress"));
  fprintf(fp, "  </sipStack>\n");

  // Locales
  fprintf(fp,
          "  <userLocale>\n    <name>%s</name>\n    <langCode>%s</langCode>\n  "
          "</userLocale>\n",
          opt_val("userLocale"), opt_val("userLocale"));
  fprintf(fp, "  <networkLocale>%s</networkLocale>\n",
          opt_val("networkLocale"));

  // Ethernet & VLANs
  fprintf(fp, "  <ethernetConfig>\n");
  char *vvlan = val("adminVlanId");
  if (vvlan[0])
    fprintf(fp, "    <adminVlanId>%s</adminVlanId>\n", vvlan);
  char *pvlan = val("pcPortVlanId");
  if (pvlan[0])
    fprintf(fp, "    <pcPortVlanId>%s</pcPortVlanId>\n", pvlan);
  fprintf(fp, "  </ethernetConfig>\n");

  // SIP Lines Loop (Still simplified iteration, but safer lookups)
  fprintf(fp, "  <sipLines>\n");
  for (int i = 0; i < total_fields; i++) {
    if (strstr(fields[i].label, "Key Function") && fields[i].opt_sel > 0) {
      int b = fields[i].label[1] - '0';
      // Next fields are sequential to Key Function in init_fields, so relative
      // index is OK for specific line block
      fprintf(fp, "    <line button=\"%d\">\n      <featureID>%s</featureID>\n",
              b, (fields[i].opt_sel == 1 ? "9" : "21"));
      fprintf(fp,
              "      <name>%s</name>\n      <displayName>%s</displayName>\n",
              fields[i + 1].value, fields[i + 2].value);

      if (fields[i].opt_sel == 1) { // Line
        fprintf(fp,
                "      <authName>%s</authName>\n      "
                "<authPassword>%s</authPassword>\n",
                fields[i + 3].value, fields[i + 4].value);
        if (fields[i + 5].opt_sel == 1)
          fprintf(fp, "      <autoAnswerEnabled>2</autoAnswerEnabled>\n      "
                      "<autoAnswerTimer>1</autoAnswerTimer>\n");
        if (fields[i + 6].value[0])
          fprintf(fp, "      <callForwardURI>%s</callForwardURI>\n",
                  fields[i + 6].value);
        if (fields[i + 7].value[0])
          fprintf(fp, "      <callPickupGroupURI>%s</callPickupGroupURI>\n",
                  fields[i + 7].value);

        // New Voicemail
        char *vm = val("voiceMailPilot");
        if (vm[0])
          fprintf(fp, "      <voiceMailPilot>%s</voiceMailPilot>\n", vm);
      }
      fprintf(fp, "    </line>\n");
    }
  }
  fprintf(fp, "  </sipLines>\n");

  // Vendor Config
  fprintf(fp, "  <vendorConfig>\n");
  fprintf(fp, "    <settingsAccess>%s</settingsAccess>\n",
          opt_val("settingsAccess"));
  fprintf(fp, "    <webAccess>%s</webAccess>\n", opt_val("webAccess"));
  fprintf(fp, "    <sshAccess>%s</sshAccess>\n", opt_val("sshAccess"));

  char *sshUser = val("sshUserId");
  if (sshUser[0])
    fprintf(fp, "    <sshUserId>%s</sshUserId>\n", sshUser);
  char *sshPass = val("sshPassword");
  if (sshPass[0])
    fprintf(fp, "    <sshPassword>%s</sshPassword>\n", sshPass);
  char *admPass = val("adminPassword");
  if (admPass[0])
    fprintf(fp, "    <adminPassword>%s</adminPassword>\n", admPass);

  fprintf(fp, "    <pcPort>%s</pcPort>\n", opt_val("pcPort"));
  fprintf(fp, "    <pcVoiceVlanAccess>%s</pcVoiceVlanAccess>\n",
          opt_val("pcVoiceVlanAccess"));
  fprintf(fp, "    <spanToPCPort>%s</spanToPCPort>\n", opt_val("spanToPCPort"));
  fprintf(fp, "    <gratuitousARP>%s</gratuitousARP>\n",
          opt_val("gratuitousARP"));

  fprintf(fp, "    <bluetooth>%s</bluetooth>\n", opt_val("bluetooth"));
  fprintf(fp, "    <bluetoothProfile>%s</bluetoothProfile>\n",
          opt_val("bluetoothProfile"));

  fprintf(fp, "    <preferredCodec>%s</preferredCodec>\n",
          opt_val("preferredCodec"));
  fprintf(fp, "    <advertiseG722Codec>%s</advertiseG722Codec>\n",
          opt_val("advertiseG722Codec"));
  char *adscp = val("dscpForAudio");
  if (adscp[0])
    fprintf(fp, "    <dscpForAudio>%s</dscpForAudio>\n", adscp);

  // RTP Range
  char *rtpMin = val("startMediaPort");
  if (rtpMin[0])
    fprintf(fp,
            "    <startMediaPort>%s</startMediaPort>\n    "
            "<stopMediaPort>%s</stopMediaPort>\n",
            rtpMin, val("stopMediaPort"));

  fprintf(fp, "    <videoCapability>%s</videoCapability>\n",
          opt_val("videoCapability"));
  fprintf(fp, "    <autoTransmitVideo>%s</autoTransmitVideo>\n",
          opt_val("autoTransmitVideo"));
  fprintf(fp, "    <videoBitRate>%s</videoBitRate>\n", opt_val("videoBitRate"));
  char *vdscp = val("dscpForVideo");
  if (vdscp[0])
    fprintf(fp, "    <dscpForVideo>%s</dscpForVideo>\n", vdscp);
  fprintf(fp, "    <rtcp>%s</rtcp>\n", opt_val("rtcp"));

  fprintf(fp, "    <dndControl>%s</dndControl>\n", opt_val("dndControl"));
  fprintf(fp, "    <dndCallAlert>%s</dndCallAlert>\n", opt_val("dndCallAlert"));
  char *dndt = val("dndReminderTimer");
  if (dndt[0])
    fprintf(fp, "    <dndReminderTimer>%s</dndReminderTimer>\n", dndt);

  Field *snmp = get_field("snmpEnabled");
  if (snmp && snmp->opt_sel == 1)
    fprintf(fp,
            "    <snmpEnable>1</snmpEnable>\n    "
            "<snmpCommunity>%s</snmpCommunity>\n",
            val("snmpCommunity"));
  char *syslog = val("syslogAddr");
  if (syslog[0])
    fprintf(fp, "    <syslogAddr>%s</syslogAddr>\n", syslog);

  char *dir = val("directoryURL");
  if (dir[0])
    fprintf(fp, "    <directoryURL>%s</directoryURL>\n", dir);
  char *svc = val("servicesURL");
  if (svc[0])
    fprintf(fp, "    <servicesURL>%s</servicesURL>\n", svc);
  char *auth = val("authenticationURL");
  if (auth[0])
    fprintf(fp, "    <authenticationURL>%s</authenticationURL>\n", auth);
  char *info = val("informationURL");
  if (info[0])
    fprintf(fp, "    <informationURL>%s</informationURL>\n", info);
  char *dial = val("dialTemplate");
  if (dial[0])
    fprintf(fp, "    <dialTemplate>%s</dialTemplate>\n", dial);
  char *soft = val("softKeyFile");
  if (soft[0])
    fprintf(fp, "    <softKeyFile>%s</softKeyFile>\n", soft);

  // New Wallpapers
  char *wall = val("idleURL");
  if (wall[0])
    fprintf(fp, "    <idleURL>%s</idleURL>\n", wall);
  char *time = val("idleTimeout");
  if (time[0])
    fprintf(fp, "    <idleTimeout>%s</idleTimeout>\n", time);

  // Common Profile (Wallpaper) - Standard method
  // Note: backgroundImage is often in <commonProfile>, but VendorConfig
  // overwrites on some firmware. For 8945, <background><image> in common
  // profile is standard, but <vendorConfig><backgroundImage> is often used for
  // forceful override. We will put it in VendorConfig to keep it simple as
  // requested.
  char *bg = val("backgroundImage");
  if (bg[0])
    fprintf(fp, "    <backgroundImage>%s</backgroundImage>\n", bg);

  fprintf(fp, "  </vendorConfig>\n");
  char *mtu = val("mtu");
  if (mtu[0])
    fprintf(fp, "  <mtu>%s</mtu>\n", mtu);
  fprintf(fp, "</device>\n");

  fclose(fp);
  mvprintw(LINES - 2, 2, "SUCCESS: %s SAVED.", fn);
  getch();
}

// --- MAIN LOOP
// ----------------------------------------------------------------------
int main() {
  // Ncurses Setup
  initscr();
  start_color();
  cbreak();
  noecho();
  keypad(stdscr, 1);
  curs_set(0);

  // Color Definitions
  // Color Definitions "Cyberpunk / Midnight"
  init_pair(1, COLOR_CYAN, COLOR_BLACK);  // Standard Text / Values
  init_pair(2, COLOR_WHITE, COLOR_BLUE);  // Header Bar (Classic Cisco Blue)
  init_pair(3, COLOR_RED, COLOR_BLACK);   // Required Label
  init_pair(4, COLOR_WHITE, COLOR_RED);   // Cursor on Required
  init_pair(5, COLOR_BLACK, COLOR_CYAN);  // Selection Highlight
  init_pair(6, COLOR_GREEN, COLOR_BLACK); // Field Labels

  // Initialize the form data
  init_fields();

  int scroll_off = 0;

  // Main Input Loop
  while (1) {
    erase();
    int h, w;
    getmaxyx(stdscr, h, w);
    int max_view = h - 6; // Reserve space for header/footer

    // Scroll Logic
    if (current < scroll_off)
      scroll_off = current;
    if (current >= scroll_off + max_view)
      scroll_off = current - max_view + 1;

    // Draw Header
    attron(COLOR_PAIR(2));
    mvprintw(0, 0,
             " CISCO 8945 CONFIG GENERATOR | Fields: %d | Created by "
             "Robert Rogers @robsyoutube ",
             total_fields);
    attroff(COLOR_PAIR(2));

    int py = 2, f_cnt = 0;

    // Draw Fields
    for (int i = 0; i < total_fields; i++) {
      if (fields[i].hidden)
        continue; // Skip hidden fields

      // Only draw if within the scroll window
      if (f_cnt >= scroll_off && py < h - 4) {
        if (fields[i].type == FT_HEADER) {
          attron(A_BOLD);
          mvprintw(py++, 2, "%s", fields[i].label);
          attroff(A_BOLD);
        } else {
          // Highlight current selection
          if (i == current) {
            // Full width bar for selection
            attron(COLOR_PAIR(5));
            mvprintw(py, 0, "%*s", w, ""); // Clear background
            mvprintw(py, 4, "%-20s : %-20s", fields[i].label, fields[i].value);
            attroff(COLOR_PAIR(5));
          } else {
            // Normal Row
            if (fields[i].type == FT_MANDATORY)
              attron(COLOR_PAIR(3));
            else
              attron(COLOR_PAIR(6));
            mvprintw(py, 4, "%-20s", fields[i].label);

            attroff(COLOR_PAIR(3));
            attroff(COLOR_PAIR(6));

            attron(COLOR_PAIR(1));
            mvprintw(py, 27, ": %-20s", fields[i].value);
            attroff(COLOR_PAIR(1));
          }
          py++;
        }
      }
      f_cnt++;
    }

    // Draw Footer Divider
    mvhline(h - 4, 0, ACS_HLINE, w);

    // Draw Help Text (2-Line Wrapper)
    mvprintw(h - 4, 2, "HELP:");
    int help_len = strlen(fields[current].help);
    if (help_len < w - 10) {
      mvprintw(h - 4, 8, "%s", fields[current].help);
    } else {
      char tmp[512];
      strncpy(tmp, fields[current].help, w - 10);
      tmp[w - 10] = '\0';
      mvprintw(h - 4, 8, "%s", tmp);
      mvprintw(h - 3, 8, "%s", fields[current].help + (w - 10));
    }

    // Input Handling
    int ch = getch();
    if (ch == 'q')
      break;
    if (ch == 's')
      save_xml();

    // Navigation
    if (ch == KEY_UP && current > 1) {
      do {
        current--;
      } while (current > 0 &&
               (fields[current].hidden || fields[current].type == FT_HEADER));
    }
    if (ch == KEY_DOWN && current < total_fields - 1) {
      do {
        current++;
      } while (current < total_fields - 1 &&
               (fields[current].hidden || fields[current].type == FT_HEADER));
    }

    // Enter Key Editing
    if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
      if (fields[current].opt_count > 0)
        show_popup(&fields[current]); // Show Dropdown
      else {
        // Text Entry using new Popup
        show_text_input(&fields[current]);
      }
    }
  }
  endwin();
  return 0;
}
