/*
 * Homemade USB Peripheral device-mode drivers
 * I know there are a lotta USB device stacks floating around in the universe, but wanted to make my own
 *  1) So I can learn about USB
 *  2) So I know the exact capabilities of the hardware/driver
 *  3) So I know exactly how the code fits into the larger application context
 */



#include "OLD_app_usb_interface.hpp"

// ============================ Construction =========================
USB_Interface::USB_Interface(const USB_Hardware_Core& hw,
                             USB_Zero_Endpoint& ep0,
                             std::array<USB_In_Endpoint*,8> in_eps,
                             std::array<USB_Out_Endpoint*,8> out_eps)
    : hw_(hw), ep0_(ep0), in_(in_eps), out_(out_eps) {}

// ============================ Registration =========================
void USB_Interface::set_descriptors_input(const USB_Descriptors_Input& in) {
    din_ = in;
    if (strings_count_ == 0) {
        strings_[0] = in.string0; // reserve index 0 for LANGID
        strings_count_ = 1;
    }
}

bool USB_Interface::add_class_function(const USB_ClassFunc_Desc& func) {
    if (funcs_count_ >= MAX_CLASS_FUNCS) return false;
    funcs_[funcs_count_++] = func;
    return true;
}

bool USB_Interface::add_string_descriptor(std::span<const uint8_t> s) {
    if (strings_count_ >= MAX_STRINGS) return false;
    strings_[strings_count_++] = s;
    return true;
}

bool USB_Interface::add_class_handler(uint8_t first_if, uint8_t num_if,
                                      Callback_Function<bool> h) {
    if (handlers_count_ >= MAX_CLASS_HANDLERS) return false;
    handlers_[handlers_count_++] = {first_if, num_if, h};
    return true;
}

// ============================ Init / Deinit =========================
void USB_Interface::init() {
    // RM0399 §60.15.1: Core reset
    hw_.core_init();
    hw_.irq_enable();
    core_soft_reset_();

    // RM0399 §60.15.2: Force device mode + speed
    force_device_mode_fs_();

    // RM0399 §60.15.3: FIFO configuration
    configure_global_fifos_(256, {128,128,128,64,0,0,0,0});

    // Mask interrupts
    hw_.core->GINTSTS = 0xFFFFFFFF;
    hw_.core->GINTMSK =
        USB_OTG_GINTMSK_USBRST   |
        USB_OTG_GINTMSK_ENUMDNEM |
        USB_OTG_GINTMSK_RXFLVLM  |
        USB_OTG_GINTMSK_IEPINT   |
        USB_OTG_GINTMSK_OEPINT   |
        USB_OTG_GINTMSK_USBSUSPM |
        USB_OTG_GINTMSK_WUIM;

    hw_.device->DIEPMSK  = USB_OTG_DIEPMSK_XFRCM | USB_OTG_DIEPMSK_TOM;
    hw_.device->DOEPMSK  = USB_OTG_DOEPMSK_XFRCM | USB_OTG_DOEPMSK_STUPM;
    hw_.device->DAINTMSK = 0;

    // Reset/init endpoints
    ep0_.reset();
    for (uint8_t i=1;i<8;i++) {
        if (in_[i])  in_[i]->reset();
        if (out_[i]) out_[i]->reset();
    }

    ep0_.init();
    hw_.device->DAINTMSK |= (1u<<0)|(1u<<16);

    for (uint8_t i=1;i<8;i++) {
        if (in_[i])  { in_[i]->init();  hw_.device->DAINTMSK |= (1u<<i); }
        if (out_[i]) { out_[i]->init(); hw_.device->DAINTMSK |= (1u<<(16+i)); }
    }

    connect();
}

void USB_Interface::deinit() {
    disconnect();
    disable_core_irqs_();
    ep0_.reset();
    for (uint8_t i=1;i<8;i++) {
        if (in_[i])  in_[i]->reset();
        if (out_[i]) out_[i]->reset();
    }
    hw_.irq_disable();
    hw_.core_deinit();
}

// ============================ Bus control ===========================
void USB_Interface::connect()    { hw_.device->DCTL &= ~USB_OTG_DCTL_SDIS; }
void USB_Interface::disconnect() { hw_.device->DCTL |=  USB_OTG_DCTL_SDIS; }

// ============================ IRQ dispatcher ========================
void USB_Interface::on_irq() {
    uint32_t pend = hw_.core->GINTSTS & hw_.core->GINTMSK;
    if (pend & USB_OTG_GINTSTS_RXFLVL) handle_rx_flvl_();
    if (pend & USB_OTG_GINTSTS_USBRST) handle_usb_reset_();
    if (pend & USB_OTG_GINTSTS_ENUMDNE) handle_enum_done_();
    if (pend & USB_OTG_GINTSTS_IEPINT)  handle_iepint_();
    if (pend & USB_OTG_GINTSTS_OEPINT)  handle_oepint_();
    hw_.core->GINTSTS = pend & ~USB_OTG_GINTSTS_RXFLVL;
}

// ============================ Control helpers ======================
void USB_Interface::set_address(uint8_t addr) {
    address_ = addr & 0x7F;
    hw_.device->DCFG = (hw_.device->DCFG & ~USB_OTG_DCFG_DAD) |
                       (static_cast<uint32_t>(address_) << USB_OTG_DCFG_DAD_Pos);
    state_ = (address_==0) ? USB_DeviceState::DEFAULT : USB_DeviceState::ADDRESS;
}

void USB_Interface::set_configured(bool cfg) {
    state_ = cfg ? USB_DeviceState::CONFIGURED :
             (address_?USB_DeviceState::ADDRESS:USB_DeviceState::DEFAULT);
}

// ============================ Low-level core =======================
void USB_Interface::core_soft_reset_() {
    while (!(hw_.core->GRSTCTL & USB_OTG_GRSTCTL_AHBIDL)) {}
    hw_.core->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    while (hw_.core->GRSTCTL & USB_OTG_GRSTCTL_CSRST) {}
}

void USB_Interface::force_device_mode_fs_() {
    hw_.core->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD;
    hw_.core->GUSBCFG = (hw_.core->GUSBCFG & ~USB_OTG_GUSBCFG_TRDT) | (9u<<USB_OTG_GUSBCFG_TRDT_Pos);
    hw_.core->GCCFG |= USB_OTG_GCCFG_PWRDWN;
    hw_.device->DCFG &= ~USB_OTG_DCFG_DSPD;
    hw_.device->DCFG |= (USB_OTG_DCFG_DSPD_0|USB_OTG_DCFG_DSPD_1);
}

void USB_Interface::configure_global_fifos_(uint16_t rx,
                                            const std::array<uint16_t,8>& tx) {
    hw_.core->GRXFSIZ = rx;
    uint32_t start = rx;
    hw_.core->GNPTXFSIZ = (tx[0]<<16)|start;
    start+=tx[0];
    for(uint8_t i=1;i<8;i++){
        if(!tx[i]) continue;
        *(&hw_.core->DIEPTXF0+(i-1)) = (tx[i]<<16)|start;
        start+=tx[i];
    }
}
void USB_Interface::enable_core_irqs_(){ hw_.core->GAHBCFG|=USB_OTG_GAHBCFG_GINT; }
void USB_Interface::disable_core_irqs_(){ hw_.core->GAHBCFG&=~USB_OTG_GAHBCFG_GINT; }

// ============================ EP0 setup handling ===================
void USB_Interface::handle_setup_(const USB_SetupPacket& s) {
    last_setup_ = s;
    const uint8_t type = (s.bmRequestType>>5)&0x03;
    const uint8_t recip = s.bmRequestType&0x1F;

    if(type==0x00){ // standard
        switch(s.bRequest){
            case 0x06: std_get_descriptor_(s); break;
            case 0x05: std_set_address_(s);    break;
            case 0x09: std_set_configuration_(s); break;
            default: ep0_.stall(); break;
        }
        return;
    }

    // class/vendor
    if(recip==1){ // interface
        uint8_t ifn = s.wIndex & 0xFF;
        for(uint8_t i=0;i<handlers_count_;i++){
            auto& h=handlers_[i];
            if(ifn>=h.first_if && ifn<h.first_if+h.num_if){
                if(h.handler()) return;
            }
        }
    }
    ep0_.stall();
}

void USB_Interface::std_get_descriptor_(const USB_SetupPacket& s) {
    std::span<const uint8_t> out;
    switch(s.wValue>>8){
        case 1: out=din_.device; break;
        case 2: compose_configuration_descriptor_();
                out={config_buf_,config_len_}; break;
        case 3: out=get_string_by_index_(s.wValue&0xFF); break;
        case 6: out=din_.device_qualifier; break;
        case 7: out=din_.other_speed_config; break;
        default: out={}; break;
    }
    if(out.empty()){ ep0_.stall(); return; }
    ep0_.send(out.first(std::min<size_t>(s.wLength,out.size())));
}
void USB_Interface::std_set_address_(const USB_SetupPacket& s){
    set_address(s.wValue&0x7F);
    ep0_.status_in();
}
void USB_Interface::std_set_configuration_(const USB_SetupPacket&){
    set_configured(true);
    ep0_.status_in();
}

// ============================ Descriptor Composer =================
void USB_Interface::compose_configuration_descriptor_() {
    config_len_=0;
    if(funcs_count_==0) return;
    // header
    uint8_t hdr[9]={9,2,0,0,0,1,0,0x80,50};
    std::memcpy(config_buf_,hdr,9); config_len_=9;
    uint8_t next_if=0;
    for(uint8_t f=0;f<funcs_count_;f++){
        auto frag=funcs_[f].block;
        size_t off=0; uint8_t assigned=0;
        while(off+2<=frag.size()){
            uint8_t len=frag[off], typ=frag[off+1];
            if(len==0||off+len>frag.size()) break;
            if(typ==4 && assigned<funcs_[f].num_interfaces){
                std::memcpy(&config_buf_[config_len_],&frag[off],len);
                config_buf_[config_len_+2]=next_if++;
                assigned++;
            } else {
                std::memcpy(&config_buf_[config_len_],&frag[off],len);
            }
            config_len_+=len;
            off+=len;
        }
    }
    config_buf_[2]=config_len_&0xFF;
    config_buf_[3]=(config_len_>>8)&0xFF;
    config_buf_[4]=next_if;
}
std::span<const uint8_t> USB_Interface::get_string_by_index_(uint8_t idx){
    if(idx>=strings_count_) return {};
    return strings_[idx];
}
