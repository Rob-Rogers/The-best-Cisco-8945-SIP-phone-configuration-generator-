The-best-Cisco-8945-SIP-phone-configuration-generator-
A full-screen, ncurses-based provisioning tool for generating SEP<MAC>.cnf.xml files for Cisco 8945 IP Phones running SIP firmware.
Built for VoIP admins, lab environments, and homelabs who want a fast, visual, and reliable way to provision Cisco phones without CUCM.

FEATURES
Full-screen TUI (Text User Interface) using ncurses
Generates valid SEP<MAC>.cnf.xml provisioning files
Smart field logic (hides/shows fields dynamically)
SIP, VLAN, NAT, QoS, Video, SNMP, Bluetooth, DND, and more
Full Cisco timezone list
Inline help and examples for every field
MAC address sanitization and validation
Supports up to 4 SIP lines / buttons

REQUIREMENTS
Linux, Windows or macOS

GCC or Clang
ncurses development library

Install ncurses

Debian / Ubuntu
apt install libncurses-dev

macOS (Homebrew)
brew install ncurses

COMPILATION

gcc -o cisco_gen CISCO8945-phone.c -lncurses

Then run:

./cisco_gen

CONTROLS

Arrow Up / Down = Move between fields
Enter = Edit field / open dropdown
s = Save XML file
q = Quit program

OUTPUT
The program generates:
SEP<MAC>.cnf.xml

Example:
SEP0007A1B2C3D4.cnf.xml

Place the file in your TFTP or HTTP provisioning root for your Cisco phone.
PROVISIONING FLOW (Typical)

Compile and run this tool
Enter:

MAC address
SIP Server (PBX)
Extension and Password
Save the XML file

Put the file in your TFTP or HTTP server root
Set DHCP Option 66 to your server IP
Reboot the phone

NOTES
This tool is designed for Cisco 8945 SIP firmware, not SCCP.
You must already have the correct SIP firmware on your TFTP or HTTP server.
Some XML tags may vary slightly depending on firmware version.
YouTube: @robsyoutube
GitHub: @robsyoutube
