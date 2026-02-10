#pragma once

#include "MeshModule.h"
#include "concurrency/OSThread.h"
#include "ProtobufModule.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class HunterModule : public ProtobufModule<meshtastic_RouteDiscovery>,private concurrency::OSThread
{
    bool firstTime = true;
    uint16_t node_id=1;

  public:
    HunterModule();


  protected:
    unsigned int my_interval = 10000; // interval in millisconds
    virtual int32_t runOnce() override;

    static void launch(NodeNum node);
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r) override;

};

extern HunterModule *hunterModule;
