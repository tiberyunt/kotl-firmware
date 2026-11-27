/* Bounded EFI variable service. SPDX-License-Identifier: MIT */
#include "efi-storage.h"
#include "services.h"
#define SLOTS 32
#define NAME_UNITS 64
#define DATA_BYTES 1024
#define STORE_BYTES 65536
#define MAGIC 0x315241564c544f4bULL
#define NV EFI_VARIABLE_NON_VOLATILE
#define BS EFI_VARIABLE_BOOTSERVICE_ACCESS
#define RT EFI_VARIABLE_RUNTIME_ACCESS
#define APPEND EFI_VARIABLE_APPEND_WRITE
#define SUPPORTED (NV | BS | RT)
typedef struct {
    EFI_GUID guid;
    uint32_t attributes, size, name_units, reserved;
    CHAR16 name[NAME_UNITS];
    uint8_t data[DATA_BYTES];
} VARIABLE;
_Static_assert(sizeof(VARIABLE) == 1184, "variable wire record");
typedef struct {
    uint64_t magic;
    uint32_t version, record_size, bytes, checksum, count, reserved[9];
    VARIABLE records[SLOTS];
    uint8_t padding[STORE_BYTES - 64 - SLOTS * sizeof(VARIABLE)];
} STORE;
_Static_assert(sizeof(STORE) == STORE_BYTES, "variable store wire format");
static VARIABLE variables[SLOTS];
static STORE wire;
static int busy;
static unsigned name_length(const CHAR16 *name, UINTN capacity)
{
    if (!name)
        return 0;
    if (capacity > NAME_UNITS)
        capacity = NAME_UNITS;
    for (unsigned i = 0; i < capacity; i++)
        if (!name[i])
            return i + 1;
    return 0;
}
static int named(const VARIABLE *value, const CHAR16 *name, const EFI_GUID *guid, unsigned units)
{
    return value->attributes && value->name_units == units &&
           !memcmp(&value->guid, guid, sizeof(*guid)) && !memcmp(value->name, name, units * 2);
}
static int find(const CHAR16 *name, const EFI_GUID *guid, unsigned units)
{
    for (unsigned i = 0; i < SLOTS; i++)
        if (named(&variables[i], name, guid, units))
            return i;
    return -1;
}
static int visible(const VARIABLE *value)
{
    return value->attributes && (boot_active || (value->attributes & RT));
}
static EFI_STATUS attributes_valid(uint32_t attributes)
{
    if (attributes & ~SUPPORTED)
        return EFI_UNSUPPORTED;
    if (!(attributes & BS))
        return EFI_INVALID_PARAMETER;
    if ((attributes & NV) && !platform_variables_persistent())
        return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}
static int valid_store(void)
{
    if (wire.magic != MAGIC || wire.version != 1 || wire.record_size != sizeof(VARIABLE) ||
        wire.bytes != STORE_BYTES || wire.count > SLOTS)
        return 0;
    uint32_t checksum = wire.checksum;
    wire.checksum = 0;
    int correct = crc32(&wire, sizeof(wire)) == checksum;
    wire.checksum = checksum;
    if (!correct)
        return 0;
    unsigned count = 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        VARIABLE *value = &wire.records[i];
        if (!value->attributes) {
            static const VARIABLE empty;
            if (memcmp(value, &empty, sizeof(empty)))
                return 0;
            continue;
        }
        if (!(value->attributes & NV) || attributes_valid(value->attributes) || !value->size ||
            value->size > DATA_BYTES || value->name_units < 2 || value->name_units > NAME_UNITS ||
            value->reserved || name_length(value->name, value->name_units) != value->name_units)
            return 0;
        for (unsigned j = 0; j < i; j++)
            if (named(&wire.records[j], value->name, &value->guid, value->name_units))
                return 0;
        count++;
    }
    return count == wire.count;
}
static EFI_STATUS persist(void)
{
    memset(&wire, 0, sizeof(wire));
    wire.magic = MAGIC;
    wire.version = 1;
    wire.record_size = sizeof(VARIABLE);
    wire.bytes = sizeof(wire);
    for (unsigned i = 0; i < SLOTS; i++)
        if (variables[i].attributes & NV) {
            wire.records[i] = variables[i];
            wire.count++;
        }
    wire.checksum = crc32(&wire, sizeof(wire));
    return platform_variables_save(&wire, sizeof(wire));
}
static EFI_STATUS get_variable(const CHAR16 *name, const EFI_GUID *guid, uint32_t *attributes,
                               UINTN *size, void *data)
{
    if (busy)
        return EFI_NOT_READY;
    unsigned units = name_length(name, NAME_UNITS);
    if (!guid || !size || units < 2)
        return EFI_INVALID_PARAMETER;
    int index = find(name, guid, units);
    if (index < 0 || !visible(&variables[index]))
        return EFI_NOT_FOUND;
    VARIABLE *value = &variables[index];
    if (attributes)
        *attributes = value->attributes;
    if (*size < value->size) {
        *size = value->size;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (!data)
        return EFI_INVALID_PARAMETER;
    *size = value->size;
    memcpy(data, value->data, value->size);
    return EFI_SUCCESS;
}
static EFI_STATUS next_variable(UINTN *size, CHAR16 *name, EFI_GUID *guid)
{
    if (busy)
        return EFI_NOT_READY;
    if (!size || !name || !guid)
        return EFI_INVALID_PARAMETER;
    unsigned units = name_length(name, *size / 2);
    if (!units)
        return EFI_INVALID_PARAMETER;
    int index = -1;
    if (units > 1) {
        index = find(name, guid, units);
        if (index < 0 || !visible(&variables[index]))
            return EFI_INVALID_PARAMETER;
    }
    for (unsigned i = index + 1; i < SLOTS; i++)
        if (visible(&variables[i])) {
            UINTN needed = variables[i].name_units * 2;
            if (*size < needed) {
                *size = needed;
                return EFI_BUFFER_TOO_SMALL;
            }
            *size = needed;
            memcpy(name, variables[i].name, needed);
            *guid = variables[i].guid;
            return EFI_SUCCESS;
        }
    return EFI_NOT_FOUND;
}
static EFI_STATUS set_variable(const CHAR16 *name, const EFI_GUID *guid, uint32_t attributes,
                               UINTN size, const void *data)
{
    if (busy)
        return EFI_NOT_READY;
    unsigned units = name_length(name, NAME_UNITS);
    if (!guid || units < 2 || (size && !data))
        return EFI_INVALID_PARAMETER;
    int append = !!(attributes & APPEND);
    attributes &= ~APPEND;
    if (attributes & ~SUPPORTED)
        return EFI_UNSUPPORTED;
    int index = find(name, guid, units);
    if (index >= 0 && !visible(&variables[index]))
        return EFI_NOT_FOUND;
    int deleting = !attributes || (!size && !append);
    if (index >= 0 && attributes && attributes != variables[index].attributes)
        return EFI_INVALID_PARAMETER;
    if (!boot_active) {
        if (index >= 0 && !(variables[index].attributes & NV))
            return EFI_WRITE_PROTECTED;
        uint32_t access = index >= 0 ? variables[index].attributes : attributes;
        if ((access & (NV | RT)) != (NV | RT))
            return EFI_INVALID_PARAMETER;
    }
    if (!deleting) {
        EFI_STATUS status = attributes_valid(attributes);
        if (status)
            return status;
        if (index >= 0 && attributes != variables[index].attributes)
            return EFI_INVALID_PARAMETER;
        if (append && !size)
            return EFI_SUCCESS;
        UINTN existing = append && index >= 0 ? variables[index].size : 0;
        if (size > DATA_BYTES - existing)
            return EFI_INVALID_PARAMETER;
    } else if (index < 0)
        return EFI_NOT_FOUND;
    if (index < 0) {
        for (unsigned i = 0; i < SLOTS; i++)
            if (!variables[i].attributes) {
                index = i;
                break;
            }
        if (index < 0)
            return EFI_OUT_OF_RESOURCES;
    }
    VARIABLE previous = variables[index];
    VARIABLE *value = &variables[index];
    busy = 1;
    if (deleting)
        memset(value, 0, sizeof(*value));
    else {
        UINTN offset = append && value->attributes ? value->size : 0;
        if (!append || !value->attributes)
            memset(value, 0, sizeof(*value));
        value->guid = *guid;
        value->attributes = attributes;
        value->name_units = units;
        memcpy(value->name, name, units * 2);
        memcpy(value->data + offset, data, size);
        value->size = offset + size;
    }
    EFI_STATUS result = EFI_SUCCESS;
    if ((previous.attributes | value->attributes) & NV)
        result = persist();
    if (result)
        *value = previous;
    busy = 0;
    return result;
}
static EFI_STATUS query_info(uint32_t attributes, uint64_t *maximum, uint64_t *remaining,
                             uint64_t *size)
{
    if (busy)
        return EFI_NOT_READY;
    if (!maximum || !remaining || !size)
        return EFI_INVALID_PARAMETER;
    EFI_STATUS status = attributes_valid(attributes);
    if (status)
        return status;
    if (!boot_active && !(attributes & RT))
        return EFI_INVALID_PARAMETER;
    unsigned free = 0;
    for (unsigned i = 0; i < SLOTS; i++)
        if (!variables[i].attributes)
            free++;
    *maximum = SLOTS * sizeof(VARIABLE);
    *remaining = free * sizeof(VARIABLE);
    *size = DATA_BYTES;
    return EFI_SUCCESS;
}
void variables_init(EFI_RUNTIME_SERVICES *runtime)
{
    memset(variables, 0, sizeof(variables));
    busy = 0;
    if (!platform_variables_load(&wire, sizeof(wire))) {
        if (valid_store()) {
            memcpy(variables, wire.records, sizeof(variables));
            puts_serial("EFI variables: loaded persistent store\n");
        } else
            puts_serial("EFI variables: empty or invalid persistent store\n");
    }
    runtime->GetVariable = get_variable;
    runtime->GetNextVariableName = next_variable;
    runtime->SetVariable = set_variable;
    runtime->QueryVariableInfo = query_info;
}
