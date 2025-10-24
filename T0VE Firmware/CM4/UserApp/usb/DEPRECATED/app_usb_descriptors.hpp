/*
 * usb_descriptors_embedded.hpp
 *
 * Matching header file for usb_descriptors_embedded.cpp
 * Provides struct declarations, enums, and factory function prototypes.
 * Designed for embedded use (no heap, constexpr-friendly).
 */

#pragma once

#include <stdint.h>

#if defined(__GNUC__)
  #define PACKED __attribute__((packed))
#else
  #define PACKED
  #pragma pack(push, 1)
#endif

/* -------------------------------------------------------------
 *  Common enums
 * ------------------------------------------------------------- */
enum class USB_Bcd_t : uint16_t;
enum class USB_Power_Source_t : uint8_t;
enum class USB_Remote_Wakeup_t : uint8_t;
enum class USB_Ep_Type_t : uint8_t;
enum class USB_Ep_Dir_t : uint8_t;
enum class USB_Class_t : uint8_t;
enum class USB_CDC_Subclass_t : uint8_t;
enum class USB_CDC_Protocol_t : uint8_t;
enum class USB_MSC_Subclass_t : uint8_t;
enum class USB_MSC_Protocol_t : uint8_t;
enum class USB_HID_Subclass_t : uint8_t;
enum class USB_HID_Protocol_t : uint8_t;
enum class USB_Desc_Type_t : uint8_t;

/* -------------------------------------------------------------
 *  Descriptor structs (forward declarations)
 * ------------------------------------------------------------- */
struct PACKED USB_Device_Descriptor_t;
struct PACKED USB_Configuration_Descriptor_t;
struct PACKED USB_Interface_Descriptor_t;
struct PACKED USB_Endpoint_Descriptor_t;
struct PACKED USB_HID_Descriptor_t;

struct PACKED USB_CDC_Header_Descriptor_t;
struct PACKED USB_CDC_CallMgmt_Descriptor_t;
struct PACKED USB_CDC_ACM_Descriptor_t;
struct PACKED USB_CDC_Union_Descriptor_t;

// Bundles
struct PACKED USB_MSC_Interface_Bundle_t;
struct PACKED USB_HID_Interface_Bundle_t;
struct PACKED USB_Vendor_Interface_Bundle_t;
struct PACKED USB_CDC_ACM_Bundle_t;

// String descriptors
struct PACKED USB_String0_Descriptor_t;
template <uint8_t kMaxChars>
struct PACKED USB_StringN_Descriptor_t;

// Example composite
struct PACKED USB_Example_Config_t;

/* -------------------------------------------------------------
 *  Factory functions (prototypes only)
 * ------------------------------------------------------------- */
USB_Device_Descriptor_t make_device_descriptor(
    USB_Bcd_t usb_bcd,
    USB_Class_t dev_class,
    uint8_t dev_subclass,
    uint8_t dev_protocol,
    uint8_t ep0_max_packet_size,
    uint16_t vid,
    uint16_t pid,
    uint16_t device_bcd,
    uint8_t idx_manufacturer,
    uint8_t idx_product,
    uint8_t idx_serial,
    uint8_t num_configurations);

USB_Configuration_Descriptor_t make_configuration_descriptor(
    uint16_t total_length_bytes,
    uint8_t num_interfaces,
    uint8_t configuration_value,
    uint8_t idx_configuration,
    USB_Power_Source_t power_source,
    USB_Remote_Wakeup_t remote_wakeup,
    uint8_t max_power_ma);

USB_Interface_Descriptor_t make_interface_descriptor(
    uint8_t interface_number,
    uint8_t num_endpoints,
    USB_Class_t cls,
    uint8_t subclass,
    uint8_t protocol,
    uint8_t alt_setting,
    uint8_t idx_interface);

USB_Endpoint_Descriptor_t make_endpoint_descriptor(
    uint8_t ep_num,
    USB_Ep_Dir_t dir,
    USB_Ep_Type_t type,
    uint16_t max_packet_size,
    uint8_t interval);

// Endpoint helpers
USB_Endpoint_Descriptor_t BULK_IN_64_EP(uint8_t ep_num);
USB_Endpoint_Descriptor_t BULK_OUT_64_EP(uint8_t ep_num);
USB_Endpoint_Descriptor_t INT_IN_EP_64(uint8_t ep_num, uint8_t interval);
USB_Endpoint_Descriptor_t INT_OUT_EP_64(uint8_t ep_num, uint8_t interval);
USB_Endpoint_Descriptor_t INT_IN_EP_8(uint8_t ep_num, uint8_t interval);
USB_Endpoint_Descriptor_t INT_OUT_EP_8(uint8_t ep_num, uint8_t interval);

// HID
USB_HID_Descriptor_t make_hid_descriptor(uint16_t hid_bcd, uint8_t country_code, uint16_t report_len);

// CDC
USB_CDC_Header_Descriptor_t make_cdc_header(uint16_t bcd_cdc);
USB_CDC_CallMgmt_Descriptor_t make_cdc_call_mgmt(uint8_t caps, uint8_t data_ifc);
USB_CDC_ACM_Descriptor_t make_cdc_acm(uint8_t caps);
USB_CDC_Union_Descriptor_t make_cdc_union(uint8_t master_ifc, uint8_t slave_ifc);

// Bundles
USB_MSC_Interface_Bundle_t make_msc_interface(uint8_t interface_number, uint8_t ep_out_num, uint8_t ep_in_num);
USB_HID_Interface_Bundle_t make_hid_interface(uint8_t interface_number, uint16_t hid_bcd, uint16_t report_len, uint8_t ep_in_num, uint8_t interval_ms, USB_HID_Subclass_t hid_subclass, USB_HID_Protocol_t hid_protocol);
USB_Vendor_Interface_Bundle_t make_vendor_interface(uint8_t interface_number, uint8_t ep_out_num, uint8_t ep_in_num);
USB_CDC_ACM_Bundle_t make_cdc_acm_interfaces(uint8_t base_interface_number, uint8_t ep_notify_in_num, uint8_t ep_data_out_num, uint8_t ep_data_in_num, uint8_t notify_interval_ms);

// Strings
USB_String0_Descriptor_t make_string0_descriptor(uint16_t langid);
template <uint8_t kMaxChars>
USB_StringN_Descriptor_t<kMaxChars> make_stringn_descriptor(const char* ascii, uint8_t len_chars);

// Example composite
USB_Example_Config_t make_example_config();

#if !defined(__GNUC__)
  #pragma pack(pop)
#endif

/* -------------------------------------------------------------
 *  Example instantiations (full descriptors)
 * ------------------------------------------------------------- */

// String descriptors (US English)
constexpr auto string0 = make_string0_descriptor(0x0409);
constexpr auto string_manufacturer = make_stringn_descriptor<16>("Acme Devices", 12);
constexpr auto string_product_cdc = make_stringn_descriptor<16>("CDC ACM VCP", 11);
constexpr auto string_product_msc = make_stringn_descriptor<16>("Mass Storage", 12);
constexpr auto string_product_combo = make_stringn_descriptor<24>("Composite CDC+MSC", 18);
constexpr auto string_serial = make_stringn_descriptor<16>("12345678", 8);

// Example 1: CDC ACM Virtual COM Port Device
constexpr USB_Device_Descriptor_t device_cdc_acm = make_device_descriptor(
    USB_Bcd_t::usb_2_0,
    USB_Class_t::cdc,
    0,
    0,
    64,
    0x1234, // VID
    0x0001, // PID
    0x0100, // Device BCD
    1, // Manufacturer string index
    2, // Product string index
    3, // Serial string index
    1  // Num configurations
);
constexpr auto config_cdc_acm = make_cdc_acm_interfaces(0, 1, 2, 3, 16);

// Example 2: Mass Storage Device
constexpr USB_Device_Descriptor_t device_msc = make_device_descriptor(
    USB_Bcd_t::usb_2_0,
    USB_Class_t::msc,
    0,
    0,
    64,
    0x1234, // VID
    0x0002, // PID
    0x0100,
    1,
    2,
    3,
    1
);
constexpr auto config_msc = make_msc_interface(0, 1, 2);

// Example 3: Composite CDC ACM + Mass Storage
constexpr USB_Device_Descriptor_t device_composite = make_device_descriptor(
    USB_Bcd_t::usb_2_0,
    USB_Class_t::use_interface,
    0,
    0,
    64,
    0x1234,
    0x0003,
    0x0100,
    1,
    2,
    3,
    1
);
constexpr auto config_composite = make_example_config();
