#include "ConsoleModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"

ConsoleModule *consoleModule;


void ConsoleModule::sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    service->sendToMesh(
        p, RX_SRC_LOCAL,
        true); // send to mesh, cc to phone. Even if there's no phone connected, this stores the message to match ACKs
}


ProcessMessage ConsoleModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Console module: text msg from=0x%0x, id=0x%x, channel=%d,  msg=%.*s", mp.from, mp.id, mp.channel, p.payload.size, p.payload.bytes);
#endif
    // se il messaggio proviene da una delle chiavi autorizzate,
    // si prosegue

    const bool authorized =
        (config.security.admin_key[0].size == 32 &&
         memcmp(mp.public_key.bytes, config.security.admin_key[0].bytes, 32) == 0) ||
        (config.security.admin_key[1].size == 32 &&
         memcmp(mp.public_key.bytes, config.security.admin_key[1].bytes, 32) == 0) ||
        (config.security.admin_key[2].size == 32 &&
         memcmp(mp.public_key.bytes, config.security.admin_key[2].bytes, 32) == 0);

    if (!authorized) {
        LOG_INFO("message from no-authorized keys");
        return ProcessMessage::CONTINUE;
    }
    LOG_INFO("message from authorized keys");

    if (p.payload.size>0 and p.payload.bytes[0]=='H') {
        LOG_INFO("help");
        this->sendText(mp.from,0, "reply from firmware!", false);
    } else if (p.payload.size>0 and p.payload.bytes[0]=='N') {
        LOG_INFO("Nodes");
        // 0 = ourself
        for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
            const auto &entry = nodeDatabase.nodes[i];

            if (entry.hops_away == 0) {
                static char tempstr[128]; //

                sprintf(tempstr, "%s: '%s' snr: %f", entry.user.short_name, entry.user.long_name, entry.snr);
                this->sendText(mp.from,0, tempstr, false);
                break;
            }
        }

    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool ConsoleModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
