#include <stddef.h>

#include "MessageJsonConverter.h"
#include "Expression.h"

static char lower(char chr) {
    if (chr >= 'A' && chr <= 'Z')
        return chr - 'A' + 'a';
}

static bool isEnumType(DataType t) {
    return t == DATATYPE;
}

aJsonObject *MessageJsonConverter::messageToJson(Message &m) {
    aJsonObject *obj = aJson.createObject();

    headerToJson(obj, m);
    payloadToJson(obj, m);

    return obj;
}

void MessageJsonConverter::headerToJson(aJsonObject *obj, Message &m) {
    uint8_t from = m.getSrcAddress();
    uint8_t to = m.getDstAddress();
    MessageType type = m.getType();
    char *typestr = NULL;

    switch (type) {
    case PUBLISH:
        typestr = "publish";
        break;
    case SET:
        typestr = "set";
        break;
    case REQUEST:
        typestr = "request";
        break;
    case ERR:
        typestr = "err";
        break;
    case GARBAGE:
        typestr = "garbage";
        break;
    default:
        typestr = "unknown";
    }

    if (typestr)
        aJson.addStringToObject(obj, "type", typestr);
    else
        aJson.addNumberToObject(obj, "type", type);
    aJson.addNumberToObject(obj, "from", from);
}

void MessageJsonConverter::payloadToJson(aJsonObject *obj, Message &m) {
    for (Message::iter i = m.begin(); i; m.iterAdvance(i)) {
        DataType t;
        uint32_t val;
        const prog_char *cname;
        char name[50];
        CodingType coding = (CodingType) -1;
        aJsonObject *parent, *child;
        const prog_char *enumId = NULL;

        m.iterGetTypeValue(i, &t, &val);
        if (t == (DataType) -1)
            continue; /* TODO: Add a note in the JSON output */

        cname = Message::dataTypeToString(t, &coding) ?: PSTR("Unknown");
        strncpy_P(name, cname, sizeof(name));
        name[0] = lower(name[0]);

        switch (coding) {
        case boolCoding:
            child = aJson.createItem((char) *(bool *) &val);
            break;
        case intCoding:
            if (isEnumType(t))
                switch (t) {
                case DATATYPE:
                    enumId = Message::dataTypeToString(
                            (DataType) *(int *) &val, NULL);
                    break;
                }

            if (enumId) {
                char buf[50];
                strncpy_P(buf, enumId, sizeof(buf));
                child = aJson.createItem(buf);
            } else
                child = aJson.createItem(*(int *) &val);
            break;
        case floatCoding:
            child = aJson.createItem((double) *(float *) &val);
            break;
        case binaryCoding:
            switch (t) {
            case MESSAGE:
                child = aJson.createObject();
                {
                    Message subMsg;
                    memcpy(subMsg.getWriteBuffer() + HEADERS_LENGTH,
                            ((BinaryValue *) &val)->value,
                            ((BinaryValue *) &val)->len);
                    subMsg.writeLength(HEADERS_LENGTH +
                            ((BinaryValue *) &val)->len);
                    payloadToJson(child, subMsg);
                }
                break;

            case EXPRESSION:
                {
                    child = aJson.createNull();
                    child->type = aJson_String;
                    child->valuestring = exprToString(
                            ((BinaryValue *) &val)->value,
                            ((BinaryValue *) &val)->len);
                }
                break;

            default:
                child = aJson.createItem("fixme");
            }
            break;
        default:
            child = aJson.createItem("fixme");
            break;
        }

        /* See if there is an item of the same name already. */
        parent = aJson.getObjectItem(obj, name); /* slow lookup :/ */

        /* If there is one and it's not an array, convert it into a
         * one-element array so we can append the new item to it.  This
         * is necessary because, even though JSON objects don't
         * explicitly disallow pairs with identical names, an object
         * is defined as being unordered meaning that the order of
         * same-typed elements would be lost, unlike in a list.
         */
        if (parent) {
            if (parent->type != aJson_Array) {
                /* Low-level but less inefficient */
                aJsonObject *copy = aJson.createNull();
                copy->type = parent->type;
                copy->child = parent->child;
                copy->valuefloat = parent->valuefloat;
                parent->type = aJson_Array;
                parent->child = NULL;
                aJson.addItemToArray(parent, copy);
            }
            aJson.addItemToArray(parent, child);
        } else
            aJson.addItemToObject(obj, name, child);
    }
}

static int messageAddElem(Message &msg, const char *name, aJsonObject *obj) {
    DataType t = Message::stringToDataType(name);
    if (t == (DataType) __INT_MAX__)
        return -1;

    CodingType coding = (CodingType) -1;
    if (!Message::dataTypeToString(t, &coding))
        return -1;

    switch (coding) {
    case boolCoding:
        if (obj->type != aJson_True && obj->type != aJson_False)
            return -1;

        msg.addBoolValue(t, obj->type == aJson_True);
        break;

    case intCoding:
        if (isEnumType(t)) {
            DataType valuetype;

            switch (t) {
            case DATATYPE:
                if (obj->type == aJson_String)
                    valuetype = Message::stringToDataType(obj->valuestring);
                else if (obj->type == aJson_Int)
                    valuetype = (DataType) obj->valueint;
                else
                    return -1;
                if (valuetype == (DataType) __INT_MAX__)
                    return -1;

                msg.addDataTypeValue(valuetype);
                break;
            }
        } else if (obj->type == aJson_Int)
            msg.addIntValue(t, obj->valueint);
        else
            return -1;

        break;

    case floatCoding:
        if (obj->type != aJson_Int && obj->type != aJson_Float)
            return -1;

        msg.addFloatValue(t, obj->type == aJson_Int ?
                obj->valueint : obj->valuefloat);
        break;

    case binaryCoding:
        switch (t) {
        case MESSAGE:
            if (obj->type != aJson_Object)
                return -1;
            {
                Message subMsg(0, 0);
                if (!MessageJsonConverter::jsonToPayload(subMsg, *obj))
                    return -1;
                msg.addBinaryValue(t,
                        subMsg.getRawData() + HEADERS_LENGTH,
                        subMsg.getRawLength() - HEADERS_LENGTH);
            }
            break;

        default:
            return -1;
        }
        break;

    default:
        return -1;
    }

    return 0;
}

Message *MessageJsonConverter::jsonToMessage(aJsonObject &obj) {
    Message *msg;
    aJsonObject *val;
    uint8_t from, to;

    if (obj.type != aJson_Object)
        return NULL;

    val = aJson.getObjectItem(&obj, "to");
    if (!val || val->type != aJson_Int)
        return NULL;
    to = val->valueint;

    val = aJson.getObjectItem(&obj, "from");
    if (val && val->type != aJson_Int)
        return NULL;
    from = val ? val->valueint : 0;

    msg = new Message(from, to);

    val = aJson.getObjectItem(&obj, "type");
    if (!val || val->type != aJson_String)
        goto err;

    if (!strcmp(val->valuestring, "publish"))
        msg->setType(PUBLISH);
    else if (!strcmp(val->valuestring, "set"))
        msg->setType(SET);
    else if (!strcmp(val->valuestring, "request"))
        msg->setType(REQUEST);
    else if (!strcmp(val->valuestring, "err"))
        msg->setType(ERR);
    else
        goto err;

    if (!jsonToPayload(*msg, obj))
        goto err;

    return msg;

err:
    delete msg;
    return NULL;
}

bool MessageJsonConverter::jsonToPayload(Message &msg, aJsonObject &obj) {
    aJsonObject *val;

    for (val = obj.child; val; val = val->next) {
        if (!strcasecmp(val->name, "to") || !strcasecmp(val->name, "from") ||
                !strcasecmp(val->name, "type")) {
            /* Skip */
            continue;
        }

        if (val->type == aJson_Array) {
            for (aJsonObject *elem = val->child; elem; elem = elem->next)
                if (messageAddElem(msg, val->name, elem))
                    return 0;
        } else
            if (messageAddElem(msg, val->name, val))
                return 0;
    }

    return 1;
}

/* TODO: pgmspace */
struct {
    uint8_t val;
    char str[2];
} opTable[] = {
    { Expression::OP_EQ, '=', '=' },
    { Expression::OP_NE, '!', '=' },
    { Expression::OP_LT, "<" },
    { Expression::OP_GT, ">" },
    { Expression::OP_LE, '<', '=' },
    { Expression::OP_GE, '>', '=' },
    { Expression::OP_OR, '|', '|' },
    { Expression::OP_AND, '&', '&' },
    { Expression::OP_ADD, "+" },
    { Expression::OP_SUB, "-" },
    { Expression::OP_MULT, "*" },
    { Expression::OP_DIV, "/" },
};

static void numDigit(char *&str, int16_t val) {
    if (!val)
        return;
    numDigit(str, val / 10);
    *str++ = '0' + (val % 10);
}

static float numDigit(char *&str, float val) {
    if (val >= 10.0f)
        val = numDigit(str, val / 10.0f) * 10.0f;
    *str++ = '0' + (int) val;
    return val - (int) val;
}

static void numToStr(char *&str, int16_t val) {
    if (val == 0)
        *str++ = '0';
    else {
        if (val < 0) {
            *str++ = '-';
            val = -val;
        }
        numDigit(str, val);
    }
}

static void numToStr(char *&str, float val) {
	uint16_t frac;

	if (val < 0.0f) {
		*str++ = '-';
		val = -val;
	}
    frac = numDigit(str, val) * 10000.0f;
	if (frac)
		*str++ = '.';
	while (frac) {
        uint8_t dig = frac / 1000;
        frac = 10 * (frac - dig * 10);
        *str++ = '0' + dig;
	}
}

void subexprToString(char *&str, const uint8_t *&buf, uint8_t &len) {
    int intVal;
    float floatVal;
    uint8_t varServId, varType, varNum, num;
    const char *name;

    if (!len) {
        memcpy_P(str, PSTR("fixme"), 5);
        str += 5;
        return;
    }

    len--;
    uint8_t op = *buf++;

    using namespace Expression;

    if (op >= OP_EQ)
        *str++ = '(';

#define SPACE *str++ = ' '

    switch (op) {
    case VAL_INT8:
        intVal = *buf++;
        len--;
        numToStr(str, intVal);
        break;

    case VAL_INT16:
        intVal = ((uint16_t) buf[0] << 8) | buf[1];
        buf += 2;
        len -= 2;
        numToStr(str, intVal);
        break;

    case VAL_FLOAT:
        *(uint32_t *) &floatVal =
            ((uint32_t) buf[0] << 24) |
            ((uint32_t) buf[1] << 16) |
            ((uint32_t) buf[2] <<  8) |
            ((uint32_t) buf[3] <<  0);
        buf += 4;
        len -= 4;
        numToStr(str, floatVal);
        break;

    case VAL_VARIABLE:
    case VAL_PREVIOUS:
        varServId = *buf++;
        varType = (DataType) *buf++;
        varNum = *buf++;
        len -= 3;

        memcpy_P(str, op == VAL_VARIABLE ? PSTR("data:") : PSTR("prev:"), 5);
        str += 5;

        numToStr(str, varServId);
        *str++ = ':';
        name = Message::dataTypeToString((DataType) varType, NULL) ?:
            PSTR("fixme");
        strcpy_P(str, name);
        str += strlen_P(name);
        *str++ = ':';
        numToStr(str, varNum);
        break;

    case OP_EQ:
    case OP_NE:
    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
    case OP_OR:
    case OP_AND:
    case OP_ADD:
    case OP_SUB:
    case OP_MULT:
    case OP_DIV:
        subexprToString(str, buf, len);
        for (num = 0; opTable[num].val != op; num++);
        SPACE;
        *str++ = opTable[num].str[0];
        if (opTable[num].str[1])
            *str++ = opTable[num].str[1];
        SPACE;
        subexprToString(str, buf, len);
        break;

    case OP_NOT:
    case OP_NEG:
        *str++ = (op == OP_NOT) ? '!' : '-';
        subexprToString(str, buf, len);
        break;

    case OP_IN:
        num = *buf++;
        len--;
        subexprToString(str, buf, len);
        memcpy_P(str, PSTR(" in "), 4);
        str += 4;

        while (--num) {
            subexprToString(str, buf, len);
            *str++ = ',';
        }
        subexprToString(str, buf, len);
        break;

    case OP_IFELSE:
        subexprToString(str, buf, len);
        SPACE;
        *str++ = '?';
        SPACE;
        subexprToString(str, buf, len);
        SPACE;
        *str++ = ':';
        SPACE;
        subexprToString(str, buf, len);
        break;

    case OP_BETWEEN:
        subexprToString(str, buf, len);
        memcpy_P(str, PSTR(" between "), 9);
        str += 9;
        subexprToString(str, buf, len);
        *str++ = ',';
        subexprToString(str, buf, len);
    }

    if (op >= OP_EQ)
        *str++ = ')';
}

char *MessageJsonConverter::exprToString(const uint8_t *buf, uint8_t len) {
    char *str = (char *) malloc(128); /* FIXME */
    char *ptr = str;

    subexprToString(ptr, buf, len);
    *ptr = '\0';

    return str;
}

MessageJsonConverter::MessageJsonConverter() {
    obj_str_len = 0;
    nest_depth = 0;
    quote = 0;
    escape = 0;
}

void MessageJsonConverter::putch(uint8_t chr) {
    if (!quote)
        if (chr <= ' ')
            return;

    if (obj_str_len >= sizeof(obj_str) - 1)
        return;

    obj_str[obj_str_len++] = chr;
    /* TODO: multibyte chars */
    if (quote && !escape && chr == '\\')
        escape = 1;
    else if (!escape && chr == '"')
        quote = !quote;
    else if (!quote) {
        if (chr == '{' || chr == '[' || chr == '(')
            nest_depth++;
        if (chr == '}' || chr == ']' || chr == ')') {
            nest_depth--;

            if ((int8_t) nest_depth <= 0) {
                /* End of JSON object detected */
                obj_str[obj_str_len++] = 0;
                if (!obj) {
                    obj = aJson.parse((char *) obj_str);
                    if (!obj)
                        obj = aJson.parse("{\"error\":\"syntaxError\"}");
                }

                obj_str_len = 0;
                nest_depth = 0;
            }
        }
    }
}
/* vim: set sw=4 ts=4 et: */
