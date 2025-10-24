#pragma once

#include <array>
#include <span>
#include <cstdint>
#include <cstring>
#include "app_usb_endpoint.hpp"
#include "app_utils.hpp"
#include "usb_otg.h"

// ============================ Constants ============================

// Tune for your application. No dynamic allocation is used.
constexpr uint8_t  MAX_CLASS_HANDLERS   = 6;    // max class drivers
constexpr uint8_t  MAX_CLASS_FUNCS      = 6;    // max descriptor fragments
constexpr size_t   MAX_CONFIG_BYTES     = 512;  // buffer for composite config
constexpr uint8_t  MAX_STRINGS          = 10;   // string table slots

// ============================ Setup Packet =========================
struct USB_SetupPacket {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

// ============================ Descriptor Inputs ====================
struct USB_Descriptors_Input {
    std::span<const uint8_t> device;          // required
    std::span<const uint8_t> string0;         // LANGID (required)
    std::span<const uint8_t> device_qualifier;    // optional
    std::span<const uint8_t> other_speed_config;  // optional
};

// ============================ Class Function =======================
struct USB_ClassFunc_Desc {
    std::span<const uint8_t> block;   // raw Interface+Class+Endpoint descriptors
    uint8_t num_interfaces = 1;       // how many interfaces this block consumes
};

// ============================ Class Handler ========================
// Each class handler covers a contiguous range of interfaces.
struct USB_ClassHandler {
    uint8_t first_if;
    uint8_t num_if;
    Callback_Function<bool> handler;
};

// ============================ Device State =========================
enum class USB_DeviceState : uint8_t { DEFAULT, ADDRESS, CONFIGURED, SUSPENDED };

// ============================ Hardware Core ========================
// Mirrors your HAL SPI style: static channels + ctor wiring.
struct USB_Hardware_Core {
    USB_OTG_GlobalTypeDef* const core;
    USB_OTG_DeviceTypeDef* const device;
    const Callback_Function<>    core_init;
    const Callback_Function<>    core_deinit;
    const Callback_Function<>    irq_enable;
    const Callback_Function<>    irq_disable;
};

// ============================ USB Interface ========================
class USB_Interface {
public:
    explicit USB_Interface(const USB_Hardware_Core& hw,
                           USB_Zero_Endpoint& ep0,
                           std::array<USB_In_Endpoint*,8> in_eps,
                           std::array<USB_Out_Endpoint*,8> out_eps);

    // ---- Lifecycle ----
    void set_descriptors_input(const USB_Descriptors_Input& in);
    bool add_class_function(const USB_ClassFunc_Desc& func);
    bool add_string_descriptor(std::span<const uint8_t> s);
    bool add_class_handler(uint8_t first_if, uint8_t num_if,
                           Callback_Function<bool> h);

    void init();      // Run full programming model (RM0399 §60.15)
    void deinit();    // Disconnect + power down

    // ---- Bus control ----
    void connect();
    void disconnect();

    // ---- Interrupt entry (call from OTG_FS_IRQHandler) ----
    void on_irq();

    // ---- Control helpers ----
    void set_address(uint8_t addr);
    void set_configured(bool configured);
    USB_DeviceState state() const { return state_; }

    // ---- Accessors for handlers ----
    const USB_SetupPacket& last_setup() const { return last_setup_; }
    USB_Zero_Endpoint& ep0() { return ep0_; }

private:
    // ---- Core bring-up helpers ----
    void core_soft_reset_();
    void force_device_mode_fs_();
    void configure_global_fifos_(uint16_t rx_words,
                                 const std::array<uint16_t,8>& tx_words);
    void enable_core_irqs_();
    void disable_core_irqs_();

    // ---- ISR subroutines ----
    void handle_usb_reset_();
    void handle_enum_done_();
    void handle_rx_flvl_();
    void handle_iepint_();
    void handle_oepint_();

    // ---- EP0 control ----
    void handle_setup_(const USB_SetupPacket& setup);
    void std_get_descriptor_(const USB_SetupPacket& setup);
    void std_set_address_(const USB_SetupPacket& setup);
    void std_set_configuration_(const USB_SetupPacket& setup);

    // ---- Descriptor composition ----
    void compose_configuration_descriptor_();
    std::span<const uint8_t> get_string_by_index_(uint8_t idx);

private:
    // Hardware
    const USB_Hardware_Core& hw_;
    USB_Zero_Endpoint& ep0_;
    std::array<USB_In_Endpoint*,8>  in_;
    std::array<USB_Out_Endpoint*,8> out_;

    // State
    USB_DeviceState state_ = USB_DeviceState::DEFAULT;
    uint8_t address_ = 0;
    USB_SetupPacket last_setup_{};

    // Descriptors
    USB_Descriptors_Input din_{};
    USB_ClassFunc_Desc funcs_[MAX_CLASS_FUNCS]{};
    uint8_t funcs_count_ = 0;
    std::span<const uint8_t> strings_[MAX_STRINGS]{};
    uint8_t strings_count_ = 0;
    alignas(4) uint8_t config_buf_[MAX_CONFIG_BYTES]{};
    size_t config_len_ = 0;

    // Handlers
    USB_ClassHandler handlers_[MAX_CLASS_HANDLERS]{};
    uint8_t handlers_count_ = 0;
};
