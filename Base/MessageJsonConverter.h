#ifndef MESSAGE_JSON_CONVERTER_H
#define MESSAGE_JSON_CONVERTER_H

#include <aJSON.h>

#include "Message.h"

class MessageJsonConverter {
public:
    /* Json parser support (stateful) */
    MessageJsonConverter();
    void putch(uint8_t chr);
    aJsonObject *obj;

    /* Message -> Json conversion (stateless) */
    static aJsonObject *messageToJson(Message &m);

    /* Json -> Message conversion (stateless) */
    static Message *jsonToMessage(aJsonObject &obj);

    /* Helpers */
    static void headerToJson(aJsonObject *obj, Message &m);
    static void payloadToJson(aJsonObject *obj, Message &m);
    static bool jsonToPayload(Message &msg, aJsonObject &obj);
    static char *exprToString(const uint8_t *buf, uint8_t len);
    static uint8_t *exprFromString(const char *str, uint8_t *len);

private:
    uint8_t obj_str[128], obj_str_len, nest_depth, escape, quote;
};

#endif // whole file
/* vim: set sw=4 ts=4 et: */
