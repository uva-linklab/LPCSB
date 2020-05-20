#!/usr/bin/env python

""" Barebones BGAPI scanner script for Bluegiga BLE modules

This script is designed to be used with a BGAPI-enabled Bluetooth Smart device
from Bluegiga, probably a BLED112 but anything that is connected to a serial
port (real or virtual) and can "speak" the BGAPI protocol. It is tuned for
usable defaults when used on a Raspberry Pi, but can easily be used on other
platforms, including Windows or OS X.

Note that the command functions do *not* incorporate the extra preceding length
byte required when using "packet" mode (only available on USART peripheral ports
on the BLE112/BLE113 module, not applicable to the BLED112). It is built so you
can simply plug in a BLED112 and go, but other kinds of usage may require small
modifications.

Changelog:
    2013-04-07 - Fixed 128-bit UUID filters
               - Added more verbose output on startup
               - Added "friendly mode" output argument
               - Added "quiet mode" output argument
               - Improved comments in code
    2013-03-30 - Initial release

"""

__author__ = "Jeff Rowberg, modified by Alexander Sarris"
__license__ = "MIT"
__version__ = "2013-04-07"
__email__ = "jeff@rowberg.net"

import sys, optparse, serial, struct, time, datetime, re, signal, csv 
from os import path
import datetime
import struct 

import math
import copy

options = []
filter_uuid = []
filter_mac = []
filter_rssi = 0

#Function for finding the median value in a table
def median(lst):
    quotient, remainder = divmod(len(lst), 2)
    if remainder:
        return sorted(lst)[quotient]
    return sum(sorted(lst)[quotient - 1:quotient + 1]) / 2.


def main():
    global options, filter_uuid, filter_mac, filter_rssi 

    class IndentedHelpFormatterWithNL(optparse.IndentedHelpFormatter):
      def format_description(self, description):
        if not description: return ""
        desc_width = self.width - self.current_indent
        indent = " "*self.current_indent
        bits = description.split('\n')
        formatted_bits = [
          optparse.textwrap.fill(bit,
            desc_width,
            initial_indent=indent,
            subsequent_indent=indent)
          for bit in bits]
        result = "\n".join(formatted_bits) + "\n"
        return result

      def format_option(self, option):
        result = []
        opts = self.option_strings[option]
        opt_width = self.help_position - self.current_indent - 2
        if len(opts) > opt_width:
          opts = "%*s%s\n" % (self.current_indent, "", opts)
          indent_first = self.help_position
        else: # start help on same line as opts
          opts = "%*s%-*s  " % (self.current_indent, "", opt_width, opts)
          indent_first = 0
        result.append(opts)
        if option.help:
          help_text = self.expand_default(option)
          help_lines = []
          for para in help_text.split("\n"):
            help_lines.extend(optparse.textwrap.wrap(para, self.help_width))
          result.append("%*s%s\n" % (
            indent_first, "", help_lines[0]))
          result.extend(["%*s%s\n" % (self.help_position, "", line)
            for line in help_lines[1:]])
        elif opts[-1] != "\n":
          result.append("\n")
        return "".join(result)

    class MyParser(optparse.OptionParser):
        def format_epilog(self, formatter=None):
            return self.epilog

        def format_option_help(self, formatter=None):
            formatter = IndentedHelpFormatterWithNL()
            if formatter is None:
                formatter = self.formatter
            formatter.store_option_strings(self)
            result = []
            result.append(formatter.format_heading(optparse._("Options")))
            formatter.indent()
            if self.option_list:
                result.append(optparse.OptionContainer.format_option_help(self, formatter))
                result.append("\n")
            for group in self.option_groups:
                result.append(group.format_help(formatter))
                result.append("\n")
            formatter.dedent()
            # Drop the last "\n", or the header if no options or option groups:
            return "".join(result[:-1])

    # process script arguments
    p = MyParser(description='Bluetooth Smart Scanner script for Bluegiga BLED112 v2013-03-30', epilog=
"""Examples:

    bled112_scanner.py

\tDefault options, passive scan, display all devices

    bled112_scanner.py -p /dev/ttyUSB0 -d sd

\tUse ttyUSB0, display only sender MAC address and ad data payload

    bled112_scanner.py -u 1809 -u 180D

\tDisplay only devices advertising Health Thermometer service (0x1809)
\tor the Heart Rate service (0x180D)

    bled112_scanner.py -m 00:07:80 -m 08:57:82:bb:27:37

\tDisplay only devices with a Bluetooth address (MAC) starting with the
\tBluegiga OUI (00:07:80), or exactly matching 08:57:82:bb:27:37

Sample Output Explanation:

    1364699494.574 -57 0 000780814494 0 255 02010603030918

    't' (Unix time):\t1364699464.574, 1364699591.128, etc.
    'r' (RSSI value):\t-57, -80, -92, etc.
    'p' (Packet type):\t0 (advertisement), 4 (scan response)
    's' (Sender MAC):\t000780535BB4, 000780814494, etc.
    'a' (Address type):\t0 (public), 1 (random)
    'b' (Bond status):\t255 (no bond), 0 to 15 if bonded
    'd' (Data payload):\t02010603030918, etc.
            See BT4.0 Core Spec for details about ad packet format

"""
    )

    # set all defaults for options
    p.set_defaults(port="COM13", baud=115200, interval=0xC8, window=0xC8, display="trpsabd", uuid=[], mac=[], rssi=0, active=False, quiet=False, friendly=False)

    # create serial port options argument group
    group = optparse.OptionGroup(p, "Serial Port Options")
    group.add_option('--port', '-p', type="string", help="Serial port device name (default /dev/cu.usbmodem11)", metavar="PORT")
    group.add_option('--baud', '-b', type="int", help="Serial port baud rate (default 115200)", metavar="BAUD")
    p.add_option_group(group)

    # create scan options argument group
    group = optparse.OptionGroup(p, "Scan Options")
    group.add_option('--interval', '-i', type="int", help="Scan interval width in units of 0.625ms (default 200)", metavar="INTERVAL")
    group.add_option('--window', '-w', type="int", help="Scan window width in units of 0.625ms (default 200)", metavar="WINDOW")
    group.add_option('--active', '-a', action="store_true", help="Perform active scan (default passive)\nNOTE: active scans result "
                                                                 "in a 'scan response' request being sent to the slave device, which "
                                                                 "should send a follow-up scan response packet. This will result in "
                                                                 "increased power consumption on the slave device.")
    p.add_option_group(group)

    # create filter options argument group
    group = optparse.OptionGroup(p, "Filter Options")
    group.add_option('--uuid', '-u', type="string", action="append", help="Service UUID(s) to match", metavar="UUID")
    group.add_option('--mac', '-m', type="string", action="append", help="MAC address(es) to match", metavar="ADDRESS")
    group.add_option('--rssi', '-r', type="int", help="RSSI minimum filter (-110 to -20), omit to disable", metavar="RSSI")
    p.add_option_group(group)

    # create output options argument group
    group = optparse.OptionGroup(p, "Output Options")
    group.add_option('--quiet', '-q', action="store_true", help="Quiet mode (suppress initial scan parameter display)")
    group.add_option('--friendly', '-f', action="store_true", help="Friendly mode (output in human-readable format)")
    group.add_option('--display', '-d', type="string", help="Display fields and order (default '%s')\n"
        "  t = Unix time, with milliseconds\n"
        "  r = RSSI measurement (signed integer)\n"
        "  p = Packet type (0 = normal, 4 = scan response)\n"
        "  s = Sender MAC address (hexadecimal)\n"
        "  a = Address type (0 = public, 1 = random)\n"
        "  b = Bonding status (255 = no bond, else bond handle)\n"
        "  d = Advertisement data payload (hexadecimal)" % p.defaults['display'], metavar="FIELDS")
    p.add_option_group(group)

    # actually parse all of the arguments
    options, arguments = p.parse_args()

    # validate any supplied MAC address filters
    for arg in options.mac:
        if re.search('[^a-fA-F0-9:]', arg):
            p.print_help()
            print "\n================================================================"
            print "Invalid MAC filter argument '%s'\n-->must be in the form AA:BB:CC:DD:EE:FF" % arg
            print "================================================================"
            exit(1)
        arg2 = arg.replace(":", "").upper()
        if (len(arg2) % 2) == 1:
            p.print_help()
            print "\n================================================================"
            print "Invalid MAC filter argument '%s'\n--> must be 1-6 full bytes in 0-padded hex form (00:01:02:03:04:05)" % arg
            print "================================================================"
            exit(1)
        mac = []
        for i in range(0, len(arg2), 2):
            mac.append(int(arg2[i : i + 2], 16))
        filter_mac.append(mac)

    # validate any supplied UUID filters
    for arg in options.uuid:
        if re.search('[^a-fA-F0-9:]', arg):
            p.print_help()
            print "\n================================================================"
            print "Invalid UUID filter argument '%s'\n--> must be 2 or 16 full bytes in 0-padded hex form (180B or 0123456789abcdef0123456789abcdef)" % arg
            print "================================================================"
            exit(1)
        arg2 = arg.replace(":", "").upper()
        if len(arg2) != 4 and len(arg2) != 32:
            p.print_help()
            print "\n================================================================"
            print "Invalid UUID filter argument '%s'\n--> must be 2 or 16 full bytes in 0-padded hex form (180B or 0123456789abcdef0123456789abcdef)" % arg
            print "================================================================"
            exit(1)
        uuid = []
        for i in range(0, len(arg2), 2):
            uuid.append(int(arg2[i : i + 2], 16))
        filter_uuid.append(uuid)

    # validate RSSI filter argument
    filter_rssi = abs(int(options.rssi))
    if filter_rssi > 0 and (filter_rssi < 20 or filter_rssi > 110):
        p.print_help()
        print "\n================================================================"
        print "Invalid RSSI filter argument '%s'\n--> must be between 20 and 110" % filter_rssi
        print "================================================================"
        exit(1)

    # validate field output options
    options.display = options.display.lower()
    if re.search('[^trpsabd]', options.display):
        p.print_help()
        print "\n================================================================"
        print "Invalid display options '%s'\n--> must be some combination of 't', 'r', 'p', 's', 'a', 'b', 'd'" % options.display
        print "================================================================"
        exit(1)

    # display scan parameter summary, if not in quiet mode
    if not(options.quiet):
        print "================================================================"
        print "BLED112 Scanner for Python v%s" % __version__
        print "================================================================"
        #p.set_defaults(port="/dev/ttyACM0", baud=115200, interval=0xC8, window=0xC8, display="trpsabd", uuid=[], mac=[], rssi=0, active=False, quiet=False, friendly=False)
        print "Serial port:\t%s" % options.port
        print "Baud rate:\t%s" % options.baud
        print "Scan interval:\t%d (%.02f ms)" % (options.interval, options.interval * 1.25)
        print "Scan window:\t%d (%.02f ms)" % (options.window, options.window * 1.25)
        print "Scan type:\t%s" % ['Passive', 'Active'][options.active]
        print "UUID filters:\t",
        if len(filter_uuid) > 0:
            print "0x%s" % ", 0x".join([''.join(['%02X' % b for b in uuid]) for uuid in filter_uuid])
        else:
            print "None"
        print "MAC filter(s):\t",
        if len(filter_mac) > 0:
            print ", ".join([':'.join(['%02X' % b for b in mac]) for mac in filter_mac])
        else:
            print "None"
        print "RSSI filter:\t",
        if filter_rssi > 0:
            print "-%d dBm minimum"% filter_rssi
        else:
            print "None"
        print "Display fields:\t-",
        field_dict = { 't':'Time', 'r':'RSSI', 'p':'Packet type', 's':'Sender MAC', 'a':'Address type', 'b':'Bond status', 'd':'Payload data' }
        print "\n\t\t- ".join([field_dict[c] for c in options.display])
        print "Friendly mode:\t%s" % ['Disabled', 'Enabled'][options.friendly]
        print "----------------------------------------------------------------"
        print "Starting scan for BLE advertisements..."

    # open serial port for BGAPI access
    try:
        ser = serial.Serial(port=options.port, baudrate=options.baud, timeout=1)
    except serial.SerialException as e:
        print "\n================================================================"
        print "Port error (name='%s', baud='%ld'): %s" % (options.port, options.baud, e)
        print "================================================================"
        exit(2)

    # flush buffers
    #print "Flushing serial I/O buffers..."
    ser.flushInput()
    ser.flushOutput()

    # disconnect if we are connected already
    #print "Disconnecting if connected..."
    ble_cmd_connection_disconnect(ser, 0)
    response = ser.read(7) # 7-byte response
    #for b in response: print '%02X' % ord(b),

    # stop advertising if we are advertising already
    #print "Exiting advertising mode if advertising..."
    ble_cmd_gap_set_mode(ser, 0, 0)
    response = ser.read(6) # 6-byte response
    #for b in response: print '%02X' % ord(b),

    # stop scanning if we are scanning already
    #print "Exiting scanning mode if scanning..."
    ble_cmd_gap_end_procedure(ser)
    response = ser.read(6) # 6-byte response
    #for b in response: print '%02X' % ord(b),

    # set scan parameters
    #print "Setting scanning parameters..."
    ble_cmd_gap_set_scan_parameters(ser, options.interval, options.window, options.active)
    response = ser.read(6) # 6-byte response
    #for b in response: print '%02X' % ord(b),

    # start scanning now
    #print "Entering scanning mode for general discoverable..."
    # Note: In 'gap_discover_limited' (0) and 'gap_discover_generic' (1) modes
    # all 'non-conforming' (without 'flags' or with incorrect 'flags' value)
    # adverizing packets are silently discarded. All packets are visible in
    # 'gap_discover_observation' (2) mode. It is helpfull for debugging.
    ble_cmd_gap_discover(ser, 2)

    while (1):
        # catch all incoming data
        while (ser.inWaiting()): bgapi_parse(ord(ser.read()));

        # don't burden the CPU
        time.sleep(0.01)

# define API commands we might use for this script
def ble_cmd_system_reset(p, boot_in_dfu):
    p.write(struct.pack('5B', 0, 1, 0, 0, boot_in_dfu))
def ble_cmd_connection_disconnect(p, connection):
    p.write(struct.pack('5B', 0, 1, 3, 0, connection))
def ble_cmd_gap_set_mode(p, discover, connect):
    p.write(struct.pack('6B', 0, 2, 6, 1, discover, connect))
def ble_cmd_gap_end_procedure(p):
    p.write(struct.pack('4B', 0, 0, 6, 4))
def ble_cmd_gap_set_scan_parameters(p, scan_interval, scan_window, active):
    p.write(struct.pack('<4BHHB', 0, 5, 6, 7, scan_interval, scan_window, active))
def ble_cmd_gap_discover(p, mode):
    p.write(struct.pack('5B', 0, 1, 6, 2, mode))

# define basic BGAPI parser
bgapi_rx_buffer = []
bgapi_rx_expected_length = 0
def bgapi_parse(b):
    global bgapi_rx_buffer, bgapi_rx_expected_length
    global PrevClear, PrevRed, PrevGreen, PrevBlue, NetClearChange, NetRedChange, NetGreenChange, NetBlueChange
    global TotalClearChange, TotalRedChange, TotalGreenChange, TotalBlueChange

    if len(bgapi_rx_buffer) == 0 and (b == 0x00 or b == 0x80):
        bgapi_rx_buffer.append(b)
    elif len(bgapi_rx_buffer) == 1:
        bgapi_rx_buffer.append(b)
        bgapi_rx_expected_length = 4 + (bgapi_rx_buffer[0] & 0x07) + bgapi_rx_buffer[1]
    elif len(bgapi_rx_buffer) > 1:
        bgapi_rx_buffer.append(b)

    #print '%02X: %d, %d' % (b, len(bgapi_rx_buffer), bgapi_rx_expected_length)
    if bgapi_rx_expected_length > 0 and len(bgapi_rx_buffer) == bgapi_rx_expected_length:
        #print '<=[ ' + ' '.join(['%02X' % b for b in bgapi_rx_buffer ]) + ' ]'
        packet_type, payload_length, packet_class, packet_command = bgapi_rx_buffer[:4]
        bgapi_rx_payload = b''.join(chr(i) for i in bgapi_rx_buffer[4:])
        if packet_type & 0x80 == 0x00: # response
            bgapi_filler = 0
        else: # event
            if packet_class == 0x06: # gap
                if packet_command == 0x00: # scan_response
                    rssi, packet_type, sender, address_type, bond, data_len = struct.unpack('<bB6sBBB', bgapi_rx_payload[:11])
                    sender = [ord(b) for b in sender]
                    data_data = [ord(b) for b in bgapi_rx_payload[11:]]
                    display = 1

                    # parse all ad fields from ad packet
                    ad_fields = []
                    this_field = []
                    ad_flags = 0
                    ad_services = []
                    ad_local_name = []
                    ad_tx_power_level = 0
                    ad_manufacturer = []

                    bytes_left = 0
                    for b in data_data:
                        if bytes_left == 0:
                            bytes_left = b
                            this_field = []
                        else:
                            this_field.append(b)
                            bytes_left = bytes_left - 1
                            if bytes_left == 0:
                                ad_fields.append(this_field)
                                if this_field[0] == 0x01: # flags
                                    ad_flags = this_field[1]
                                if this_field[0] == 0x02 or this_field[0] == 0x03: # partial or complete list of 16-bit UUIDs
                                    for i in xrange((len(this_field) - 1) / 2):
                                        ad_services.append(this_field[-1 - i*2 : -3 - i*2 : -1])
                                if this_field[0] == 0x04 or this_field[0] == 0x05: # partial or complete list of 32-bit UUIDs
                                    for i in xrange((len(this_field) - 1) / 4):
                                        ad_services.append(this_field[-1 - i*4 : -5 - i*4 : -1])
                                if this_field[0] == 0x06 or this_field[0] == 0x07: # partial or complete list of 128-bit UUIDs
                                    for i in xrange((len(this_field) - 1) / 16):
                                        ad_services.append(this_field[-1 - i*16 : -17 - i*16 : -1])
                                if this_field[0] == 0x08 or this_field[0] == 0x09: # shortened or complete local name
                                    ad_local_name = this_field[1:]
                                if this_field[0] == 0x0A: # TX power level
                                    ad_tx_power_level = this_field[1]

                                # OTHER AD PACKET TYPES NOT HANDLED YET

                                if this_field[0] == 0xFF: # manufactuerer specific data
                                    ad_manufacturer.append(this_field[1:])

                    if len(filter_mac) > 0:
                        match = 0
                        for mac in filter_mac:
                            if mac == sender[:-len(mac) - 1:-1]:
                                match = 1
                                #mac_id= mac
                                break
                            

                        if match == 0: display = 0

                    if display and len(filter_uuid) > 0:
                        if not [i for i in filter_uuid if i in ad_services]: display = 0

                    if display and filter_rssi > 0:
                        if -filter_rssi > rssi: display = 0

                    


                    if display:
                        #print "gap_scan_response: rssi: %d, packet_type: %d, sender: %s, address_type: %d, bond: %d, data_len: %d" % \
                        #    (rssi, packet_type, ':'.join(['%02X' % ord(b) for b in sender[::-1]]), address_type, bond, data_len)
                        t = datetime.datetime.now()

                        disp_list = []
                        header=["device","device_id","received_time","sequence_no","rssi", "Light Type","Color Temp",
                                "Lux","Red","Green","Blue","Clear", "Comparing Ratios"]
                        for c in options.display:
                            if c == 't':
                                #disp_list.append("%ld.%03ld" % (time.mktime(t.timetuple()), t.microsecond/1000))
                                disp_list.append(datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%fZ"))
                            elif c == 'r':
                                disp_list.append("%d" % rssi)
                            elif c == 'p':
                                disp_list.append("%d" % packet_type)
                            elif c == 's':
                                disp_list.append("%s" % ''.join(['%02X' % b for b in sender[::-1]]))
                            elif c == 'a':
                                disp_list.append("%d" % address_type)
                            elif c == 'b':
                                disp_list.append("%d" % bond)
                            elif c == 'd': # Payload is appended here
                                disp_list.append("%s" % ''.join(['%02X' % b for b in data_data]))

                        #My added variables:                  
                        #Check the sensor ID first - 
                        # print ' '.join(disp_list)
                        
                        #Check to see if the MAC Address is for LPCSB_Test, LPCSB_0, or LPCSB_1
                        if  disp_list[3]=='C098E5405D4C' or disp_list[3]=='C098E54034A4' or disp_list[3]=='C098E540606C':                    
                            sensorID = int(disp_list[-1][(16):(18)], 16);
                            
                            # If the sensor ID is NOT 0x44 (decimal 68), ignore it, otherwise process it
                            if(sensorID != 68):
                                print("Malformed packet!")

                            else:
                                # print(sensorID)

                                #Sequence Number
                                seqNum = int(disp_list[-1][(42):(46)], 16);
                                print(seqNum)

                                #Clear
                                Clear = int(disp_list[-1][(18):(22)], 16)
                                # print(Clear)

                                #Red
                                Red = int(disp_list[-1][(22):(26)], 16)
                                # print(Red)

                                #Green
                                Green = int(disp_list[-1][(26):(30)], 16)
                                # print(Green)

                                #Blue
                                Blue = int(disp_list[-1][(30):(34)], 16)
                                # print(Blue)

                                #Color Temperature
                                ColorTemp = int(disp_list[-1][(34):(38)], 16);
                                # print(ColorTemp)

                                #Lux
                                Lux = int(disp_list[-1][(38):(42)], 16); 
                                # print(Lux)
                            
                                #Figure out what the type of light hitting the sensor is - incandescent, fluorescent, LED, or unknown
                                Lux_val = float(Lux)
                                Clear_val = float(Clear)
                                Red_val = float(Red)
                                Green_val = float(Green)
                                Blue_val = float(Blue)
                                ColorVals = [Red_val, Green_val, Blue_val]

                                maxRatio = max(ColorVals) / median(ColorVals) #Ratio between the maximum and median color values
                                minRatio = median(ColorVals) / min(ColorVals) #Ratio between the median and minimum color values

                                RatioCompare = max(maxRatio, minRatio) / min(maxRatio, minRatio)
                                # print(RatioCompare)
                                
                                if(seqNum != 1): #If the sequence number and previous values are NOT zero
                                    #Measure the rate of change of the Raw Color Values. Not using absolute 
                                    ClearChange = Clear_val - PrevClear
                                    RedChange = Red_val - PrevRed
                                    GreenChange = Green_val - PrevGreen
                                    BlueChange = Blue_val - PrevBlue

                                   #Add the change to the net
                                    NetClearChange = NetClearChange + ClearChange
                                    NetRedChange = NetRedChange + RedChange
                                    NetGreenChange = NetGreenChange + GreenChange
                                    NetBlueChange = NetBlueChange + BlueChange

                                    # Add absolute value of change to the total.
                                    TotalClearChange = TotalClearChange + abs(ClearChange)
                                    TotalRedChange = TotalRedChange + abs(RedChange)
                                    TotalGreenChange = TotalGreenChange + abs(GreenChange)
                                    TotalBlueChange = TotalBlueChange + abs(BlueChange)

                                else: #Re-initialize everything to zero
                                    PrevClear = 0
                                    PrevRed = 0
                                    PrevGreen = 0
                                    PrevBlue = 0                               

                                    NetClearChange = 0
                                    NetRedChange = 0
                                    NetGreenChange = 0
                                    NetBlueChange = 0

                                    TotalClearChange = 0
                                    TotalRedChange = 0
                                    TotalGreenChange = 0
                                    TotalBlueChange = 0                                    

                                #Make a copy of the raw values without changing the originals
                                PrevClear = copy.copy(Clear_val)
                                PrevRed = copy.copy(Red_val)
                                PrevGreen = copy.copy(Green_val)
                                PrevBlue = copy.copy(Blue_val)

                                #Is the bulb type Incandescent? Check to see if red is the highest and if green and blue are almost on top of each other
                                if Red_val == max(ColorVals) and (maxRatio) >= 1.15 and (minRatio) <= 1.05: #Determined by observing color graph data and ratios for each control bulb
                                    BulbType = "Incandescent"                   # Incandescent light is the easiest bulb to ID: red is always the highest and the other two
                                                                                # colors are always on top of each other
                                
                                #Is the bulb type fluorescent?
                                elif (Green_val == max(ColorVals) or (Red_val == max(ColorVals) and Green_val == median(ColorVals) and maxRatio <= 1.10)) and max(ColorVals) < 10000:
                                    BulbType = "Fluorescent"

                                # Leds have relatively steady color values (so do incandescent but they have a specific color template that LEDs don't).
                                # LEDs also have the lowest intensity of the different lights measured so far
                                elif (abs(NetRedChange) <= 200 and abs(NetGreenChange) <= 200 and abs(NetBlueChange) <= 200) and Lux_val <= 2000:
                                    BulbType = "LED"
                            
                                # Sunlight has higher raw color values than all of the artificial lights tested
                                # and is also the only light type to have blue as the highest raw value
                                elif Blue_val == max(ColorVals) or (Blue_val == median(ColorVals) and maxRatio <= 1.05):
                                    BulbType = "Sunlight"

                                else:
                                    BulbType = "Unknown"
                                
                                print(NetRedChange)
                                print(NetGreenChange)
                                print(NetBlueChange)
                                print(Green_val)
                                print(Red_val)
                                print(Blue_val)
                                print(Lux_val)
                                print(BulbType)

                                #The info in the below array is what is saved to the CSV file
                                lines=["LPCSB_1",disp_list[3], disp_list[0], seqNum, disp_list[1], BulbType, ColorTemp, Lux, Red, Green, Blue, Clear, RatioCompare] 

                                if not path.exists("20200315 LPCSB_1 Ceiling ID.csv"): #Test run on 20200205 is a CLOUDY / overcast day - no sun peeking through
                                    with open("20200315 LPCSB_1 Ceiling ID.csv", "w") as f:
                                        writer = csv.writer(f, delimiter=',')
                                        writer.writerow(header) # write the header
                                        # write the actual  content line by line
                                else:
                                    with open("20200315 LPCSB_1 Ceiling ID.csv", "a") as f:
                                        writer = csv.writer(f, delimiter=',')
                                        writer.writerow(lines)

        bgapi_rx_buffer = []

# gracefully exit without a big exception message if possible
def ctrl_c_handler(signal, frame):
    #print 'Goodbye, cruel world!'
    exit(0)

signal.signal(signal.SIGINT, ctrl_c_handler)

if __name__ == '__main__':
    main()