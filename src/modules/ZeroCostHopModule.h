#pragma once
#include "Observer.h"
#include "SinglePortModule.h"

class ZeroCostHopModule  : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{

    public:

    ZeroCostHopModule() : SinglePortModule("zerocosthop", meshtastic_PortNum_NODEINFO_APP) {
        LOG_INFO("zerocosthop module");
    }

    static bool isInfrastructureRole(meshtastic_Config_DeviceConfig_Role role);

    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

};

extern ZeroCostHopModule *zeroCostHopModule;