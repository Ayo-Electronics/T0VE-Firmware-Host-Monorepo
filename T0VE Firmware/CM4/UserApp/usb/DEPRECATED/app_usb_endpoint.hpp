/*
 * app_usb_endpoint.hpp
 *
 * Defines endpoint classes for STM32H7 USB FS device controller.
 * Each endpoint type has its own configuration and helpers, but all
 * inherit from USB_Endpoint. Endpoints expose minimal register-level
 * programming while hiding the hardware quirks of the STM32 USB OTG FS core.
 *
 * Classes:
 *   - USB_Endpoint: abstract base
 *   - USB_In_Endpoint: IN (device->host) endpoints
 *   - USB_Out_Endpoint: OUT (host->device) endpoints
 *   - USB_Zero_Endpoint: endpoint zero (bidirectional control)
 */

#pragma once

#include "app_registers.hpp"
#include "app_utils.hpp"
#include "app_types.hpp"
#include "app_threading.hpp"
#include "usb_otg.h"


//#### FORWARD DECLARATION OF USB INTERFACE CLASS ####
//useful for friendship
class USB_Interface;

// Endpoint direction
enum class USB_EP_Dir_t  : uint8_t { OUT = 0, IN = 1 };
// Endpoint transfer type
enum class USB_EP_Packet_t : uint8_t { CONTROL=0, ISOCHRONOUS=1, BULK=2, INTERRUPT=3 };

// ============================================================================
// Base endpoint class
// ============================================================================
class USB_Endpoint {
public:
    USB_Endpoint(uint8_t _ep_num, USB_EP_Dir_t _dir,
                 USB_EP_Packet_t _packet_type, size_t _packet_size) :
        ep_num(_ep_num),
		dir(_dir),
        packet_type(_packet_type),
		packet_size(_packet_size),
		cb(),
		DEV_REGS((USB_OTG_DeviceTypeDef*)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))	//some pointer fuckery to get this to work, how STM32 LL drivers do it
    {}
    virtual ~USB_Endpoint() = default;

    inline uint8_t        number() const { return ep_num; }
    inline uint16_t       mps()    const { return packet_size; }
    inline USB_EP_Packet_t type()  const { return packet_type; }

    // Attach callback invoked when transfer complete or data available
    inline void attach_callback(Callback_Function<> _cb) { cb = _cb; }
    inline void fire_callback() { cb(); }

    // Stall / clear stall primitives, implemented by subclasses
    virtual bool stall() = 0;
    virtual bool clear_stall() = 0;


protected:
    friend class USB_Interface;
    //virtual functions that each endpoint implements
    //called from USB_Interface class, different for in vs. out endpoints
    //by default, don't do anything
    virtual void on_reset() 	{};
    virtual void on_enum_cplt() {};
    virtual void activate()		{};
    virtual void deactivate()	{};

    const size_t          ep_num;        // Endpoint number (0–7)
    const USB_EP_Dir_t    dir;           // Direction
    const USB_EP_Packet_t packet_type;   // Transfer type
    const size_t          packet_size;   // Max packet size

    Callback_Function<>   cb = {};       // ISR callback hook

    USB_OTG_DeviceTypeDef* DEV_REGS;	 // pointers to USB device registers that we'll use to modify peripheral
};

// ============================================================================
// IN endpoints (device → host)
// ============================================================================
class USB_In_Endpoint : virtual public USB_Endpoint {
public:
    USB_In_Endpoint(uint8_t ep_num, USB_EP_Packet_t packet_type,
                    size_t packet_size, size_t fifo_words);

    // Configure registers for this IN endpoint
    void init();

    // Write up to packet_size bytes into FIFO, return bytes written
    size_t write(std::span<const uint8_t> tx);

    // Stall / clear stall primitives
    bool stall() override;
    bool clear_stall() override;

protected:
    Register<uint32_t> DIEPCTL_Register;   // Control register
    Register<uint32_t> DIEPINT_Register;   // Interrupt register
    Register<uint32_t> DIEPTSIZ_Register;  // Transfer size
    Register<uint32_t> DTXFSTS_Register;   // FIFO status
    Register<uint32_t> TX_FIFO;            // Data FIFO

    const size_t TX_FIFO_SIZE = 0;         // Configured FIFO size in words

    // Scratch buffer for word-aligned FIFO writes
    static constexpr size_t scratch_size = 64;
    alignas(4) uint8_t scratch[scratch_size] = {0};
};

// ============================================================================
// OUT endpoints (host → device)
// ============================================================================
class USB_Out_Endpoint : virtual public USB_Endpoint {
public:
    USB_Out_Endpoint(uint8_t ep_num, USB_EP_Packet_t packet_type, size_t packet_size);

    // Configure registers for this OUT endpoint
    void init();

    // Number of bytes currently available in FIFO
    size_t available() const;

    // Read up to rx.size() bytes into rx
    size_t read(std::span<uint8_t> rx);

    // Called by IRQ when data pending in FIFO
    void mark_out_pending(size_t len);

    // Stall / clear stall primitives
    bool stall() override;
    bool clear_stall() override;

protected:
    //implementing virtual functions
    void on_reset() override;


    // Prime OUT endpoint to be ready for next transfer
    void prime();

    Register<uint32_t> DOEPCTL_Register;   // Control register
    Register<uint32_t> DOEPINT_Register;   // Interrupt register
    Register<uint32_t> DOEPTSIZ_Register;  // Transfer size
    Register<uint32_t> RX_FIFO;            // Data FIFO

    Atomic_Var<size_t> rx_bytes_available = {0};         // Bytes buffered in FIFO

    // Scratch buffer for aligned word reads
    static constexpr size_t scratch_size = 64;
    alignas(4) uint8_t scratch[scratch_size] = {0};
};

// ============================================================================
// Endpoint zero (control) — both IN and OUT sides
// ============================================================================
class USB_Zero_Endpoint : public USB_In_Endpoint, public USB_Out_Endpoint {
public:
    USB_Zero_Endpoint(size_t fifo_words);

    // Configure both IN/OUT for control
    void init();

    // Control transfer helpers
    size_t send(std::span<const uint8_t> data);
    size_t expect(std::span<uint8_t> data);
    void   status_in();
    void   status_out();

    // Stall / clear stall both directions
    bool   stall() override;
    bool   clear_stall() override;

protected:
    //implementing virtual functions
    void on_reset();
};
