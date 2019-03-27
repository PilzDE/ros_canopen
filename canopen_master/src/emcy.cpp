#include <canopen_master/canopen.h>
#include <socketcan_interface/string.h>

using namespace canopen;

#pragma pack(push) /* push current alignment to stack */
#pragma pack(1) /* set alignment to 1 byte boundary */

struct EMCYid{
    uint32_t id:29;
    uint32_t extended:1;
    uint32_t :1;
    uint32_t invalid:1;
    EMCYid(uint32_t val){
        *(uint32_t*) this = val;
    }
    can::Header header() {
        return can::Header(id, extended, false, false);
    }
    const uint32_t get() const { return *(uint32_t*) this; }
};

struct EMCYfield{
    uint32_t error_code:16;
    uint32_t addition_info:16;
    EMCYfield(uint32_t val){
        *(uint32_t*) this = val;
    }
};

struct EMCYmsg{
    uint16_t error_code;
    uint8_t error_register;
    uint8_t manufacturer_specific_error_field[5];

    struct Frame: public FrameOverlay<EMCYmsg>{
        Frame(const can::Frame &f) : FrameOverlay(f){ }
    };
};

#pragma pack(pop) /* pop previous alignment from stack */

void EMCYHandler::handleEMCY(const can::Frame & msg){
    EMCYmsg::Frame em(msg);
    LOG("EMCY: " << can::tostring(msg, false));
    has_error_ = (em.data.error_register & ~32) != 0;
}

EMCYHandler::EMCYHandler(const can::CommInterfaceSharedPtr interface, const ObjectStorageSharedPtr storage): Layer("EMCY handler"), storage_(storage), has_error_(true){
    storage_->entry(error_register_, 0x1001);
    try{
        storage_->entry(num_errors_, 0x1003,0);
    }
    catch(...){
       // pass, 1003 is optional
    }
    try{
        EMCYid emcy_id(storage_->entry<uint32_t>(0x1014).get_cached());
        emcy_listener_ = interface->createMsgListener( emcy_id.header(), can::CommInterface::FrameDelegate(this, &EMCYHandler::handleEMCY));
    }
    catch(...){
       // pass, EMCY is optional
    }
}

void EMCYHandler::handleRead(LayerStatus &status, const LayerState &current_state) {
    if(current_state == Ready){
        if(has_error_){
            status.error("Node has emergency error");
        }
    }
}

void EMCYHandler::handleWrite(LayerStatus &status, const LayerState &current_state) {
    // noithing to do
}

void EMCYHandler::handleDiag(LayerReport &report){
    uint8_t error_register = 0;
    if(!error_register_.get(error_register)){
        report.error("Could not read error error_register");
        return;
    }

    if(error_register){
        if(error_register & 1){ // first bit should be set on all errors
            report.error("Node has emergency error");
        }else if(error_register & ~32){ // filter profile-specific bit
            report.warn("Error register is not zero");
        }
        report.add("error_register", (uint32_t) error_register);

        uint8_t num = num_errors_.valid() ? num_errors_.get() : 0;
        std::stringstream buf;
        for(size_t i = 0; i < num; ++i) {
            if( i!= 0){
                buf << ", ";
            }
            try{
                ObjectStorage::Entry<uint32_t> error;
                storage_->entry(error, 0x1003,i+1);
                EMCYfield field(error.get());
                buf << std::hex << field.error_code << "#" << field.addition_info;
            }
            catch (const std::out_of_range & e){
                buf << "NOT_IN_DICT!";
            }
            catch (const TimeoutException & e){
                buf << "LIST_UNDERFLOW!";
                break;
            }

        }
        report.add("errors", buf.str());

    }
}

void EMCYHandler::handleInit(LayerStatus &status){
    uint8_t error_register = 0;
    if(!error_register_.get(error_register)){
        status.error("Could not read error error_register");
        return;
    }else if(error_register & 1){
        std::stringstream buf;
        buf << "ER: ";
        resolveErrorCode(buf, int(error_register));
        LOG(buf.str());
        status.error("Node has emergency error");
        return;
    }

    resetErrors(status);
}

void EMCYHandler::resolveErrorCode(std::stringstream &buf, int error_code){
    typedef std::map<std::pair<int, int>, std::string> error_code_defintion;
    const error_code_defintion error_map = {
        /* https://www.canopensolutions.com/english/about_canopen/emergency.shtml
          code,   mask,    Error description*/
        {{0x0000, 0xFF00}, "No Error"},
        {{0x1000, 0xFF00}, "Generic Error"},
        {{0x2000, 0xF000}, "Current"},
        {{0x3000, 0xF000}, "Voltage"},
        {{0x4000, 0xF000}, "Temperature"},
        {{0x5000, 0xFF00}, "Device Hardware"},
        {{0x6000, 0xF000}, "Device Software"},
        {{0x7000, 0xFF00}, "Additional Modules"},
        {{0x8000, 0xF000}, "Monitoring"},
        {{0x9000, 0xFF00}, "External Error"},
        {{0xF000, 0xFF00}, "Additional Functions"},
        {{0xFF00, 0xFF00}, "Device Specific"}
    };
    error_code_defintion::const_iterator it = error_map.begin();
    char hex[12];
    sprintf(hex, " %#08x ", error_code);
    buf << hex;
    for(; it != error_map.end(); ++it){
        /* mask                              code */
        if(it->first.second & error_code == it->first.first)
            buf << it->second;
            break;
    }
}

void EMCYHandler::resetErrors(LayerStatus &status){
    if(num_errors_.valid()) num_errors_.set(0);
    has_error_ = false;
}

void EMCYHandler::handleRecover(LayerStatus &status){
    handleInit(status);
}

void EMCYHandler::handleShutdown(LayerStatus &status){
}

void EMCYHandler::handleHalt(LayerStatus &status){
    // do nothing
}
