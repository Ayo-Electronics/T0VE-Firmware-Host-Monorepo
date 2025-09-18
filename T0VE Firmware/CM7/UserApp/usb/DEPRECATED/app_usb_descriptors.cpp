/*
 * app_usb_descriptors.hpp
 *
 *  Created on: Sep 17, 2025
 *      Author: govis
 *
 *  Generated with ChatGPT
 */

/*
 * usb_descriptors_embedded.cpp
 *
 * Purpose:
 *   Embedded-friendly, allocation-free helpers to build common USB descriptors.
 *   Each descriptor is a tightly-packed POD struct with a constexpr factory
 *   constructor function for clarity and safety. The code aims to:
 *     - Replace magic numbers with strongly-typed enum classes.
 *     - Provide readable, minimal-callsite constructors that auto-deduce fields
 *       where possible (e.g., endpoint address, descriptor lengths, flags).
 *     - Support both single-class and composite devices.
 *     - Avoid heap usage and dynamic allocation. All APIs are constexpr-friendly.
 *
 * Style:
 *   - Types: Struct_Name_t
 *   - Variables: var_name
 *   - Functions: function_name()
 *
 * Notes:
 *   - These definitions match USB 2.0 specification descriptor layouts.
 *   - All structs are packed (no padding) and use fixed-width integer types.
 *   - Host endianness is assumed little-endian; multi-byte fields are sent
 *     on the bus in little-endian as stored.
 *   - Keep this in a translation unit (or header) compiled with warnings high.
 */

#include <stdint.h>

#if defined(__GNUC__)
  #define PACKED __attribute__((packed))
#else
  #define PACKED
  #pragma pack(push, 1)
#endif

/* -------------------------------------------------------------
 *  Common enums replacing magic numbers
 * ------------------------------------------------------------- */

// USB specification version BCD helpers (e.g., 0x0200 for USB 2.0)
enum class USB_Bcd_t : uint16_t {
  usb_1_1 = 0x0110,
  usb_2_0 = 0x0200,
  usb_2_1 = 0x0210,
  usb_3_0 = 0x0300,
};

// Power source clarity
enum class USB_Power_Source_t : uint8_t {
  bus_powered  = 0,
  self_powered = 1,
};

// Remote wakeup boolean as readable enum
enum class USB_Remote_Wakeup_t : uint8_t {
  disabled = 0,
  enabled  = 1,
};

// Endpoint transfer types
enum class USB_Ep_Type_t : uint8_t {
  control     = 0b00,
  isochronous = 0b01,
  bulk        = 0b10,
  interrupt   = 0b11,
};

// Endpoint directions (bit7 of bEndpointAddress)
enum class USB_Ep_Dir_t : uint8_t {
  out = 0x00,
  in  = 0x80,
};

// Device classes (subset; extend as needed)
enum class USB_Class_t : uint8_t {
  use_interface = 0x00, // Class is defined at interface level
  cdc           = 0x02, // Communications and CDC Control
  hid           = 0x03,
  msc           = 0x08, // Mass Storage
  vendor        = 0xFF,
};

// CDC subclass/protocol (comm interface)
enum class USB_CDC_Subclass_t : uint8_t { abstract_control_model = 0x02 };
enum class USB_CDC_Protocol_t : uint8_t { at_commands = 0x01, none = 0x00 };

// MSC subclass/protocol
enum class USB_MSC_Subclass_t : uint8_t { scsi = 0x06 };
enum class USB_MSC_Protocol_t : uint8_t { bulk_only_transport = 0x50 };

// HID subclass/protocol
enum class USB_HID_Subclass_t : uint8_t { none = 0x00, boot = 0x01 };
enum class USB_HID_Protocol_t : uint8_t { none = 0x00, keyboard = 0x01, mouse = 0x02 };

// Descriptor types
enum class USB_Desc_Type_t : uint8_t {
  device                    = 1,
  configuration             = 2,
  string_                   = 3,
  interface                 = 4,
  endpoint                  = 5,
  device_qualifier          = 6,
  other_speed_configuration = 7,
  interface_power           = 8,
  hid                       = 0x21,
  hid_report                = 0x22,
  cdc_cs_interface          = 0x24,
  cdc_cs_endpoint           = 0x25,
};

/* -------------------------------------------------------------
 *  Small helpers
 * ------------------------------------------------------------- */
static inline constexpr uint8_t make_ep_address(uint8_t ep_num, USB_Ep_Dir_t dir)
{
  return static_cast<uint8_t>((ep_num & 0x0F) | static_cast<uint8_t>(dir));
}

static inline constexpr uint8_t make_ep_attributes(USB_Ep_Type_t type)
{
  return static_cast<uint8_t>(type) & 0x03; // Bits 1:0
}

static inline constexpr uint8_t config_attributes(USB_Power_Source_t power, USB_Remote_Wakeup_t rw)
{
  // bit7 must be 1 per spec; bit6 self-powered; bit5 remote-wakeup
  const uint8_t always_1  = 1u << 7;
  const uint8_t self_pwr  = (power == USB_Power_Source_t::self_powered) ? (1u << 6) : 0;
  const uint8_t remote_wu = (rw == USB_Remote_Wakeup_t::enabled) ? (1u << 5) : 0;
  return static_cast<uint8_t>(always_1 | self_pwr | remote_wu);
}

/* -------------------------------------------------------------
 *  USB Device Descriptor
 *  Describes the overall device identity visible to the host.
 *  Fields:
 *    bLength, bDescriptorType, bcdUSB, bDeviceClass/SubClass/Protocol,
 *    bMaxPacketSize0, idVendor, idProduct, bcdDevice,
 *    iManufacturer/iProduct/iSerialNumber (string indices), bNumConfigurations
 * ------------------------------------------------------------- */
struct PACKED USB_Device_Descriptor_t {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t bcdUSB;
  uint8_t  bDeviceClass;
  uint8_t  bDeviceSubClass;
  uint8_t  bDeviceProtocol;
  uint8_t  bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t  iManufacturer;
  uint8_t  iProduct;
  uint8_t  iSerialNumber;
  uint8_t  bNumConfigurations;
};

static inline constexpr USB_Device_Descriptor_t make_device_descriptor(
    USB_Bcd_t usb_bcd,
    USB_Class_t dev_class,
    uint8_t dev_subclass, // often 0 if class at interface
    uint8_t dev_protocol, // often 0 if class at interface
    uint8_t ep0_max_packet_size,
    uint16_t vid,
    uint16_t pid,
    uint16_t device_bcd,
    uint8_t idx_manufacturer,
    uint8_t idx_product,
    uint8_t idx_serial,
    uint8_t num_configurations)
{
  return USB_Device_Descriptor_t{
    /*bLength*/            18,
    /*bDescriptorType*/    static_cast<uint8_t>(USB_Desc_Type_t::device),
    /*bcdUSB*/             static_cast<uint16_t>(usb_bcd),
    /*bDeviceClass*/       static_cast<uint8_t>(dev_class),
    /*bDeviceSubClass*/    dev_subclass,
    /*bDeviceProtocol*/    dev_protocol,
    /*bMaxPacketSize0*/    ep0_max_packet_size,
    /*idVendor*/           vid,
    /*idProduct*/          pid,
    /*bcdDevice*/          device_bcd,
    /*iManufacturer*/      idx_manufacturer,
    /*iProduct*/           idx_product,
    /*iSerialNumber*/      idx_serial,
    /*bNumConfigurations*/ num_configurations
  };
}

/* -------------------------------------------------------------
 *  USB Configuration Descriptor
 *  Represents a device configuration. bNumInterfaces should match the total
 *  number of interfaces in this configuration; wTotalLength is the combined
 *  length of config + all subordinate descriptors.
 *  Fields:
 *    bLength, bDescriptorType, wTotalLength, bNumInterfaces, bConfigurationValue,
 *    iConfiguration (string index), bmAttributes (power/wakeup), bMaxPower (2mA units)
 * ------------------------------------------------------------- */
struct PACKED USB_Configuration_Descriptor_t {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t wTotalLength;
  uint8_t  bNumInterfaces;
  uint8_t  bConfigurationValue;
  uint8_t  iConfiguration;
  uint8_t  bmAttributes;
  uint8_t  bMaxPower; // units of 2mA
};

static inline constexpr USB_Configuration_Descriptor_t make_configuration_descriptor(
    uint16_t total_length_bytes,
    uint8_t num_interfaces,
    uint8_t configuration_value,
    uint8_t idx_configuration,
    USB_Power_Source_t power_source,
    USB_Remote_Wakeup_t remote_wakeup,
    uint8_t max_power_ma // provided in mA for human readability
)
{
  const uint8_t max_power_encoded = (uint8_t)(max_power_ma / 2u); // spec: 2mA units
  return USB_Configuration_Descriptor_t{
    /*bLength*/            9,
    /*bDescriptorType*/    static_cast<uint8_t>(USB_Desc_Type_t::configuration),
    /*wTotalLength*/       total_length_bytes,
    /*bNumInterfaces*/     num_interfaces,
    /*bConfigurationValue*/configuration_value,
    /*iConfiguration*/     idx_configuration,
    /*bmAttributes*/       config_attributes(power_source, remote_wakeup),
    /*bMaxPower*/          max_power_encoded
  };
}

/* -------------------------------------------------------------
 *  USB Interface Descriptor
 *  Identifies a function within the device. For composite devices, multiple
 *  interfaces are present. bInterfaceClass/SubClass/Protocol define the
 *  function; endpoint descriptors follow this descriptor in the descriptor tree.
 *  Fields:
 *    bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting,
 *    bNumEndpoints, bInterfaceClass/SubClass/Protocol, iInterface
 * ------------------------------------------------------------- */
struct PACKED USB_Interface_Descriptor_t {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
};

static inline constexpr USB_Interface_Descriptor_t make_interface_descriptor(
    uint8_t interface_number,
    uint8_t num_endpoints,
    USB_Class_t cls,
    uint8_t subclass,
    uint8_t protocol,
    uint8_t alt_setting,
    uint8_t idx_interface)
{
  return USB_Interface_Descriptor_t{
    /*bLength*/           9,
    /*bDescriptorType*/   static_cast<uint8_t>(USB_Desc_Type_t::interface),
    /*bInterfaceNumber*/  interface_number,
    /*bAlternateSetting*/ alt_setting,
    /*bNumEndpoints*/     num_endpoints,
    /*bInterfaceClass*/   static_cast<uint8_t>(cls),
    /*bInterfaceSubClass*/subclass,
    /*bInterfaceProtocol*/protocol,
    /*iInterface*/        idx_interface
  };
}

/* -------------------------------------------------------------
 *  USB Endpoint Descriptor
 *  Describes an endpoint belonging to an interface.
 *  Fields:
 *    bLength, bDescriptorType, bEndpointAddress (num+dir), bmAttributes (type),
 *    wMaxPacketSize, bInterval
 * ------------------------------------------------------------- */
struct PACKED USB_Endpoint_Descriptor_t {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bEndpointAddress; // bit7=dir, bits3:0=ep number
  uint8_t  bmAttributes;     // bits1:0=type
  uint16_t wMaxPacketSize;
  uint8_t  bInterval;        // for interrupt/isochronous
};

static inline constexpr USB_Endpoint_Descriptor_t make_endpoint_descriptor(
    uint8_t ep_num,
    USB_Ep_Dir_t dir,
    USB_Ep_Type_t type,
    uint16_t max_packet_size,
    uint8_t interval)
{
  return USB_Endpoint_Descriptor_t{
    /*bLength*/          7,
    /*bDescriptorType*/  static_cast<uint8_t>(USB_Desc_Type_t::endpoint),
    /*bEndpointAddress*/ make_ep_address(ep_num, dir),
    /*bmAttributes*/     make_ep_attributes(type),
    /*wMaxPacketSize*/   max_packet_size,
    /*bInterval*/        interval,
  };
}

// Convenience endpoint factories (requested patterns)
static inline constexpr USB_Endpoint_Descriptor_t BULK_IN_64_EP(uint8_t ep_num)
{ return make_endpoint_descriptor(ep_num, USB_Ep_Dir_t::in, USB_Ep_Type_t::bulk, 64, 0); }

static inline constexpr USB_Endpoint_Descriptor_t BULK_OUT_64_EP(uint8_t ep_num)
{ return make_endpoint_descriptor(ep_num, USB_Ep_Dir_t::out, USB_Ep_Type_t::bulk, 64, 0); }

static inline constexpr USB_Endpoint_Descriptor_t INT_IN_EP_64(uint8_t ep_num, uint8_t interval)
{ return make_endpoint_descriptor(ep_num, USB_Ep_Dir_t::in, USB_Ep_Type_t::interrupt, 64, interval); }

static inline constexpr USB_Endpoint_Descriptor_t INT_OUT_EP_64(uint8_t ep_num, uint8_t interval)
{ return make_endpoint_descriptor(ep_num, USB_Ep_Dir_t::out, USB_Ep_Type_t::interrupt, 64, interval); }

static inline constexpr USB_Endpoint_Descriptor_t INT_IN_EP_8(uint8_t ep_num, uint8_t interval)
{ return make_endpoint_descriptor(ep_num, USB_Ep_Dir_t::in, USB_Ep_Type_t::interrupt, 8, interval); }

static inline constexpr USB_Endpoint_Descriptor_t INT_OUT_EP_8(uint8_t ep_num, uint8_t interval)
{ return make_endpoint_descriptor(ep_num, USB_Ep_Dir_t::out, USB_Ep_Type_t::interrupt, 8, interval); }

/* -------------------------------------------------------------
 *  HID Descriptor (class-specific)
 *  Sits immediately after the interface descriptor for HID.
 *  Fields:
 *    bLength, bDescriptorType(0x21), bcdHID, bCountryCode, bNumDescriptors,
 *    bReportDescriptorType(0x22), wDescriptorLength
 * ------------------------------------------------------------- */
struct PACKED USB_HID_Descriptor_t {
  uint8_t  bLength;
  uint8_t  bDescriptorType; // 0x21
  uint16_t bcdHID;
  uint8_t  bCountryCode;
  uint8_t  bNumDescriptors;
  uint8_t  bReportDescriptorType; // 0x22
  uint16_t wDescriptorLength;     // size of report descriptor
};

static inline constexpr USB_HID_Descriptor_t make_hid_descriptor(
    uint16_t hid_bcd, uint8_t country_code, uint16_t report_len)
{
  return USB_HID_Descriptor_t{
    /*bLength*/               9,
    /*bDescriptorType*/       static_cast<uint8_t>(USB_Desc_Type_t::hid),
    /*bcdHID*/                hid_bcd,
    /*bCountryCode*/          country_code,
    /*bNumDescriptors*/       1,
    /*bReportDescriptorType*/ static_cast<uint8_t>(USB_Desc_Type_t::hid_report),
    /*wDescriptorLength*/     report_len
  };
}

/* -------------------------------------------------------------
 *  Minimal CDC (ACM) class-specific descriptors
 *  - Header, Call Management, ACM, Union (comm IF to data IF)
 * ------------------------------------------------------------- */
struct PACKED USB_CDC_Header_Descriptor_t {
  uint8_t  bFunctionLength;
  uint8_t  bDescriptorType; // 0x24
  uint8_t  bDescriptorSubType; // 0x00 header
  uint16_t bcdCDC;
};

struct PACKED USB_CDC_CallMgmt_Descriptor_t {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType; // 0x24
  uint8_t bDescriptorSubType; // 0x01 call management
  uint8_t bmCapabilities; // bit0: handle call mgmt itself, bit1: over Data IF
  uint8_t bDataInterface;
};

struct PACKED USB_CDC_ACM_Descriptor_t {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType; // 0x24
  uint8_t bDescriptorSubType; // 0x02 ACM
  uint8_t bmCapabilities; // e.g., line coding, break, etc.
};

struct PACKED USB_CDC_Union_Descriptor_t {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType; // 0x24
  uint8_t bDescriptorSubType; // 0x06 union
  uint8_t bMasterInterface; // Comm IF
  uint8_t bSlaveInterface0; // Data IF
};

static inline constexpr USB_CDC_Header_Descriptor_t make_cdc_header(uint16_t bcd_cdc)
{ return USB_CDC_Header_Descriptor_t{5, static_cast<uint8_t>(USB_Desc_Type_t::cdc_cs_interface), 0x00, bcd_cdc}; }

static inline constexpr USB_CDC_CallMgmt_Descriptor_t make_cdc_call_mgmt(uint8_t caps, uint8_t data_ifc)
{ return USB_CDC_CallMgmt_Descriptor_t{5, static_cast<uint8_t>(USB_Desc_Type_t::cdc_cs_interface), 0x01, caps, data_ifc}; }

static inline constexpr USB_CDC_ACM_Descriptor_t make_cdc_acm(uint8_t caps)
{ return USB_CDC_ACM_Descriptor_t{4, static_cast<uint8_t>(USB_Desc_Type_t::cdc_cs_interface), 0x02, caps}; }

static inline constexpr USB_CDC_Union_Descriptor_t make_cdc_union(uint8_t master_ifc, uint8_t slave_ifc)
{ return USB_CDC_Union_Descriptor_t{5, static_cast<uint8_t>(USB_Desc_Type_t::cdc_cs_interface), 0x06, master_ifc, slave_ifc}; }

/* -------------------------------------------------------------
 *  "Bundles" for common interface setups
 *  These structs gather the interface descriptor and its required class/endpoint
 *  descriptors so you can emit them linearly in the descriptor table.
 * ------------------------------------------------------------- */

/* Mass Storage Class (MSC) interface: 1 interface, 2 endpoints (Bulk IN/OUT) */
struct PACKED USB_MSC_Interface_Bundle_t {
  USB_Interface_Descriptor_t     ifc;
  USB_Endpoint_Descriptor_t      ep_out;
  USB_Endpoint_Descriptor_t      ep_in;
};

static inline constexpr USB_MSC_Interface_Bundle_t make_msc_interface(uint8_t interface_number, uint8_t ep_out_num, uint8_t ep_in_num)
{
  return USB_MSC_Interface_Bundle_t{
    /*ifc*/    make_interface_descriptor(
                 interface_number,
                 /*num_endpoints*/2,
                 USB_Class_t::msc,
                 static_cast<uint8_t>(USB_MSC_Subclass_t::scsi),
                 static_cast<uint8_t>(USB_MSC_Protocol_t::bulk_only_transport),
                 /*alt*/0,
                 /*iInterface*/0),
    /*ep_out*/ BULK_OUT_64_EP(ep_out_num),
    /*ep_in*/  BULK_IN_64_EP(ep_in_num),
  };
}

/* HID interface: 1 interface, 1 interrupt IN endpoint (typical), optional OUT */
struct PACKED USB_HID_Interface_Bundle_t {
  USB_Interface_Descriptor_t ifc;
  USB_HID_Descriptor_t       hid;
  USB_Endpoint_Descriptor_t  ep_in;
  // Optional: add ep_out if needed by report map
};

static inline constexpr USB_HID_Interface_Bundle_t make_hid_interface(
    uint8_t interface_number,
    uint16_t hid_bcd,
    uint16_t report_len,
    uint8_t ep_in_num,
    uint8_t interval_ms,
    USB_HID_Subclass_t hid_subclass = USB_HID_Subclass_t::none,
    USB_HID_Protocol_t hid_protocol = USB_HID_Protocol_t::none)
{
  (void)hid_subclass; // placed in interface descriptor below
  (void)hid_protocol;
  return USB_HID_Interface_Bundle_t{
    /*ifc*/   make_interface_descriptor(
                interface_number,
                /*num_endpoints*/1,
                USB_Class_t::hid,
                static_cast<uint8_t>(hid_subclass),
                static_cast<uint8_t>(hid_protocol),
                /*alt*/0,
                /*iInterface*/0),
    /*hid*/   make_hid_descriptor(hid_bcd, /*country*/0, report_len),
    /*ep_in*/ INT_IN_EP_8(ep_in_num, /*interval*/interval_ms),
  };
}

/* Vendor-specific simple interface: 1 interface, 2 endpoints (Bulk IN/OUT) */
struct PACKED USB_Vendor_Interface_Bundle_t {
  USB_Interface_Descriptor_t ifc;
  USB_Endpoint_Descriptor_t  ep_out;
  USB_Endpoint_Descriptor_t  ep_in;
};

static inline constexpr USB_Vendor_Interface_Bundle_t make_vendor_interface(uint8_t interface_number, uint8_t ep_out_num, uint8_t ep_in_num)
{
  return USB_Vendor_Interface_Bundle_t{
    /*ifc*/   make_interface_descriptor(
                interface_number,
                /*num_endpoints*/2,
                USB_Class_t::vendor,
                /*subclass*/0,
                /*protocol*/0,
                /*alt*/0,
                /*iInterface*/0),
    /*ep_out*/ BULK_OUT_64_EP(ep_out_num),
    /*ep_in*/  BULK_IN_64_EP(ep_in_num),
  };
}

/* CDC ACM requires TWO interfaces: Communication (with INT IN) + Data (with BULK IN/OUT) */
struct PACKED USB_CDC_ACM_Bundle_t {
  // Communication Interface (master)
  USB_Interface_Descriptor_t     ifc_comm;
  USB_CDC_Header_Descriptor_t    cdc_header;
  USB_CDC_CallMgmt_Descriptor_t  cdc_call;
  USB_CDC_ACM_Descriptor_t       cdc_acm;
  USB_CDC_Union_Descriptor_t     cdc_union;
  USB_Endpoint_Descriptor_t      ep_notify_in; // Interrupt IN

  // Data Interface (slave)
  USB_Interface_Descriptor_t     ifc_data;
  USB_Endpoint_Descriptor_t      ep_out;
  USB_Endpoint_Descriptor_t      ep_in;
};

static inline constexpr USB_CDC_ACM_Bundle_t make_cdc_acm_interfaces(
    uint8_t base_interface_number, // use N for comm, N+1 for data
    uint8_t ep_notify_in_num,
    uint8_t ep_data_out_num,
    uint8_t ep_data_in_num,
    uint8_t notify_interval_ms)
{
  const uint8_t ifc_comm_num = base_interface_number;
  const uint8_t ifc_data_num = (uint8_t)(base_interface_number + 1u);

  return USB_CDC_ACM_Bundle_t{
    /*ifc_comm*/   make_interface_descriptor(
                     ifc_comm_num,
                     /*num_endpoints*/1,
                     USB_Class_t::cdc,
                     static_cast<uint8_t>(USB_CDC_Subclass_t::abstract_control_model),
                     static_cast<uint8_t>(USB_CDC_Protocol_t::none),
                     /*alt*/0,
                     /*iInterface*/0),
    /*cdc_header*/ make_cdc_header(0x0110), // CDC v1.10
    /*cdc_call*/   make_cdc_call_mgmt(/*caps*/0x00, /*data_ifc*/ifc_data_num),
    /*cdc_acm*/    make_cdc_acm(/*caps*/0x02), // Set_Line_Coding, etc.
    /*cdc_union*/  make_cdc_union(ifc_comm_num, ifc_data_num),
    /*ep_notify*/  INT_IN_EP_64(ep_notify_in_num, notify_interval_ms),

    /*ifc_data*/   make_interface_descriptor(
                     ifc_data_num,
                     /*num_endpoints*/2,
                     USB_Class_t::use_interface,
                     /*subclass*/0,
                     /*protocol*/0,
                     /*alt*/0,
                     /*iInterface*/0),
    /*ep_out*/     BULK_OUT_64_EP(ep_data_out_num),
    /*ep_in*/      BULK_IN_64_EP(ep_data_in_num),
  };
}

/* -------------------------------------------------------------
 *  String Descriptor helpers
 *  Provide human-readable manufacturer/product/serial. Index assignment is up
 *  to the application; these helpers emit language ID 0x0409 (US English) and
 *  UTF-16LE strings.
 * ------------------------------------------------------------- */
struct PACKED USB_String0_Descriptor_t {
  uint8_t  bLength;
  uint8_t  bDescriptorType; // 0x03
  uint16_t wLANGID0;        // e.g., 0x0409
};

static inline constexpr USB_String0_Descriptor_t make_string0_descriptor(uint16_t langid)
{
  return USB_String0_Descriptor_t{ 4, static_cast<uint8_t>(USB_Desc_Type_t::string_), langid };
}

// Generic string descriptor container with fixed max chars (compile-time)
template <uint8_t kMaxChars>
struct PACKED USB_StringN_Descriptor_t {
  uint8_t  bLength;           // 2 + 2*len
  uint8_t  bDescriptorType;   // 0x03
  uint16_t wString[kMaxChars];
};

// Build UTF-16LE from ASCII (subset). len_chars is number of characters to emit.
template <uint8_t kMaxChars>
static inline constexpr USB_StringN_Descriptor_t<kMaxChars> make_stringn_descriptor(const char* ascii, uint8_t len_chars)
{
  USB_StringN_Descriptor_t<kMaxChars> desc = {};
  const uint8_t n = (len_chars > kMaxChars) ? kMaxChars : len_chars;
  desc.bLength = (uint8_t)(2u + 2u * n);
  desc.bDescriptorType = static_cast<uint8_t>(USB_Desc_Type_t::string_);
  for (uint8_t i = 0; i < n; ++i) {
    desc.wString[i] = (uint16_t)(uint8_t)ascii[i];
  }
  return desc;
}

/* -------------------------------------------------------------
 *  Example: composing a single-configuration, composite device
 *  (CDC ACM + MSC). The application computes wTotalLength as sizeof of a
 *  contiguous table of descriptors laid out in the order required by the spec:
 *    Config, (Interface + class + eps) ...
 *  These examples are constexpr-capable and allocation-free.
 * ------------------------------------------------------------- */

struct PACKED USB_Example_Config_t {
  USB_Configuration_Descriptor_t cfg;
  // CDC occupies 2 interfaces (0,1), MSC is interface 2
  USB_CDC_ACM_Bundle_t           cdc;
  USB_MSC_Interface_Bundle_t     msc;
};

static inline constexpr USB_Example_Config_t make_example_config()
{
  constexpr uint8_t cfg_value = 1;
  constexpr uint8_t total_interfaces = 3; // CDC:2 + MSC:1

  const USB_CDC_ACM_Bundle_t cdc = make_cdc_acm_interfaces(
      /*base_if*/0, /*ep_notify_in*/1, /*ep_data_out*/2, /*ep_data_in*/3, /*notify_int_ms*/16);

  const USB_MSC_Interface_Bundle_t msc = make_msc_interface(
      /*ifc*/2, /*ep_out*/4, /*ep_in*/5);

  const uint16_t total_length = (uint16_t)(
      sizeof(USB_Configuration_Descriptor_t) +
      sizeof(USB_CDC_ACM_Bundle_t) +
      sizeof(USB_MSC_Interface_Bundle_t));

  return USB_Example_Config_t{
    /*cfg*/ make_configuration_descriptor(
               total_length, total_interfaces, cfg_value,
               /*iConfiguration*/0,
               USB_Power_Source_t::bus_powered,
               USB_Remote_Wakeup_t::disabled,
               /*max_power_ma*/100),
    /*cdc*/ cdc,
    /*msc*/ msc,
  };
}

/* -------------------------------------------------------------
 *  Static size checks (fail early if layouts drift)
 * ------------------------------------------------------------- */
static_assert(sizeof(USB_Device_Descriptor_t)        == 18, "Device desc size");
static_assert(sizeof(USB_Configuration_Descriptor_t) == 9,  "Config desc size");
static_assert(sizeof(USB_Interface_Descriptor_t)     == 9,  "IF desc size");
static_assert(sizeof(USB_Endpoint_Descriptor_t)      == 7,  "EP desc size");
static_assert(sizeof(USB_HID_Descriptor_t)           == 9,  "HID desc size");

#if !defined(__GNUC__)
  #pragma pack(pop)
#endif

/* -------------------------------------------------------------
 *  Usage notes
 *  - Emit the descriptors in the order mandated by the USB spec.
 *  - For composite devices, increment interface numbers accordingly.
 *  - Keep endpoint numbers unique per direction.
 *  - For speed-specific differences (FS/HS), you can provide alternative
 *    helpers or conditional compilation branches to change packet sizes.
 *  - All functions are constexpr-friendly for placement in flash/ROM.
 * ------------------------------------------------------------- */
