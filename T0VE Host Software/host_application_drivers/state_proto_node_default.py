'''
This module provides some flavors of default NodeState messages ready for protobuf serialization or conversion to a dictionary
These include:
 - default_command_empty:
    - status fields to their default values
    - no command fields set
    - magic number set to 0xA5A5A5A5 (correct value as of writing)

 - default_all:
    - status fields to their default values
        - repeated fields initialized to appropriate expected sizes
    - command fields set safe initial values, none present
    - magic number set to 0xA5A5A5A5 (correct value as of writing)

 - default_all_no_eeprom:
    - status fields to their default values
        - repeated fields initialized to appropriate expected sizes
    - command fields set safe initial values, none present
    - eeprom command fields set to default values
    - magic number set to 0xA5A5A5A5 (correct value as of writing)
'''

from host_application_drivers.state_proto_defs import *

class NodeStateDefaults:
    #default magic number 
    MAGIC_NUMBER = 0xA5A5A5A5

    @staticmethod
    def default_command_empty() -> NodeState:
        #looks dumb, but creates a default node state that deserializes correctly
        node_state = NodeState().from_dict(NodeState().to_dict(include_default_values=True))

        # #have to explicitly construct all subsystem submessages so the NanoPB decoder is happy
        # node_state.state_supervisor = StateSupervisor()
        # node_state.multicard = Multicard()
        # node_state.pm_onboard = Pm()
        # node_state.pm_motherboard = Pm()
        # node_state.offset_ctrl = OffsetCtrl()
        # node_state.hispeed = Hispeed()
        # node_state.cob_temp = CoBTemp()
        # node_state.cob_eeprom = CoBEeprom()
        # node_state.waveguide_bias = WgBias()
        # node_state.neural_mem_manager = NeuralMem()
        # node_state.comms = Comms()
        
        # #have to explicitly construct all status submessages so that the NanoPB decoder is happy
        # node_state.state_supervisor.status = StateSupervisorStatus()
        # node_state.multicard.status = MulticardStatus()
        # node_state.pm_onboard.status = PmStatus()
        # node_state.pm_motherboard.status = PmStatus()
        # node_state.offset_ctrl.status = OffsetCtrlStatus()
        # node_state.hispeed.status = HispeedStatus()
        # node_state.cob_temp.status = CoBTempStatus()
        # node_state.cob_eeprom.status = CoBEepromStatus()
        # node_state.waveguide_bias.status = WgBiasStatus()
        # node_state.neural_mem_manager.status = NeuralMemStatus()
        # node_state.comms.status = CommsStatus()

        #pop in the magic number to make sure firmware is decoding correctly
        node_state.magic_number = NodeStateDefaults.MAGIC_NUMBER
    
        # have to explicitly size all status list submessages correctly--betterproto doesn't have the concept of 
        # fixed size lists 
        #offset control
        node_state.offset_ctrl.status.offset_readback = Uint324(values=[0, 0, 0, 0]) #initialized to appropriate expected size
        
        #hispeed subsystem
        node_state.hispeed.status.tia_adc_readback = Uint324(values=[0, 0, 0, 0]) #initialized to appropriate expected size

        #waveguide bias
        node_state.waveguide_bias.status.setpoints_readback = WgBiasSetpoints()
        node_state.waveguide_bias.status.setpoints_readback.stub_setpoint = Uint3210(values=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0]) #initialized to appropriate expected size
        node_state.waveguide_bias.status.setpoints_readback.mid_setpoint = Uint324(values=[0, 0, 0, 0])   #initialized to appropriate expected size
        node_state.waveguide_bias.status.setpoints_readback.bulk_setpoint = Uint322(values=[0, 0])  #initialized to appropriate expected size

        return node_state

    @staticmethod
    def default_all_no_eeprom() -> NodeState:
        # return a default node state with a correct magic number and safe initial command fields, but no eeprom command fields
        # repeated status fields are initialized to the expected size
        node_state = NodeStateDefaults.default_command_empty()

        #system reset
        node_state.do_system_reset = False
        
        #state supervisor -- NO COMMANDS
        # node_state.state_supervisor

        #multicard
        node_state.multicard.command.sel_pd_input_aux_npic = False

        #power monitor onboard
        node_state.pm_onboard.command.regulator_enable = False

        #power monitor motherboard
        node_state.pm_motherboard.command.regulator_enable = False

        #offset control
        node_state.offset_ctrl.command.do_readback = False
        node_state.offset_ctrl.command.offset_set = Uint324(values=[0, 0, 0, 0])
        
        #hispeed subsystem
        node_state.hispeed.command.arm_request = False
        node_state.hispeed.command.load_test_sequence = False
        node_state.hispeed.command.soa_enable = Bool4(values=[False, False, False, False])
        node_state.hispeed.command.tia_enable = Bool4(values=[False, False, False, False])
        node_state.hispeed.command.soa_dac_drive = Uint324(values=[0, 0, 0, 0])

        #CoB Temperature --> NO COMMANDS AVAILABLE
        # node_state.cob_temp

        #CoB EEPROM - NO COMMANDS --> LOCKING OUT
        # node_state.cob_eeprom.command.do_cob_write_desc = False
        # node_state.cob_eeprom.command.cob_desc_set = ""
        # node_state.cob_eeprom.command.cob_write_key = 0

        #waveguide bias
        node_state.waveguide_bias.command.setpoints = WgBiasSetpoints()
        node_state.waveguide_bias.command.setpoints.stub_setpoint = Uint3210(values=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        node_state.waveguide_bias.command.setpoints.mid_setpoint = Uint324(values=[0, 0, 0, 0])
        node_state.waveguide_bias.command.setpoints.bulk_setpoint = Uint322(values=[0, 0])
        node_state.waveguide_bias.command.regulator_enable = False
        node_state.waveguide_bias.command.do_readback = False

        #memory manager
        node_state.neural_mem_manager.command.check_io_size = False
        node_state.neural_mem_manager.command.load_test_pattern = 0
        
        #comms command
        node_state.comms.command.allow_connection = True
        
        node_state.do_system_reset = False

        return node_state

    def default_all(self) -> NodeState:
        #return a default node state with a correct magic number and safe initial command fields
        node_state = NodeStateDefaults.default_all_no_eeprom()

        #fill in the EEPROM command values
        node_state.cob_eeprom.command.do_cob_write_desc = False
        node_state.cob_eeprom.command.cob_desc_set = ""
        node_state.cob_eeprom.command.cob_write_key = 0

        return node_state