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
    bool enabled = false;
    unsigned int trace_interval = 30000; // traceroute interval in milliseconds

  protected:
    virtual int32_t runOnce() override;
    static void launch(NodeNum node);
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r) override;
    void printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest, bool isTowardsDestination);
};

extern HunterModule *hunterModule;
