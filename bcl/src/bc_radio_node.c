#include <bc_radio_node.h>

__attribute__((weak)) void bc_radio_node_on_state_set(uint64_t *id, uint8_t state_id, bool *state) { (void) id; (void) state_id; (void) state; }
__attribute__((weak)) void bc_radio_node_on_state_get(uint64_t *id, uint8_t state_id) { (void) id; (void) state_id; }
__attribute__((weak)) void bc_radio_node_on_buffer(uint64_t *id, void *buffer, size_t length) { (void) id; (void) buffer; (void) length; }

bool bc_radio_node_state_set(uint64_t *id, uint8_t state_id, bool *state)
{
    uint8_t buffer[1 + BC_RADIO_ID_SIZE + sizeof(state_id) + sizeof(*state)];

    buffer[0] = BC_RADIO_HEADER_NODE_STATE_SET;

    uint8_t *pbuffer = bc_radio_id_to_buffer(id, buffer + 1);

    pbuffer[0] = state_id;

    bc_radio_bool_to_buffer(state, pbuffer + 1);

    return bc_radio_pub_queue_put(buffer, sizeof(buffer));
}

bool bc_radio_node_state_get(uint64_t *id, uint8_t state_id)
{
    uint8_t buffer[1 + BC_RADIO_ID_SIZE + sizeof(state_id)];

    buffer[0] = BC_RADIO_HEADER_NODE_STATE_GET;

    uint8_t *pbuffer = bc_radio_id_to_buffer(id, buffer + 1);

    pbuffer[0] = state_id;

    return bc_radio_pub_queue_put(buffer, sizeof(buffer));
}

bool bc_radio_node_buffer(uint64_t *id, void *buffer, size_t length)
{
    uint8_t qbuffer[BC_RADIO_NODE_MAX_BUFFER_SIZE + 1];

    if (length > BC_RADIO_NODE_MAX_BUFFER_SIZE)
    {
        return false;
    }

    qbuffer[0] = BC_RADIO_HEADER_NODE_BUFFER;
    bc_radio_id_to_buffer(id, qbuffer + 1);

    memcpy(qbuffer + 1 + BC_RADIO_ID_SIZE, buffer, length);

    return bc_radio_pub_queue_put(qbuffer, length + 1 + BC_RADIO_ID_SIZE);
}

void bc_radio_node_decode(uint64_t *id, uint8_t *buffer, size_t length)
{
    (void) id;

    uint64_t for_id;

    if (length < BC_RADIO_ID_SIZE + 1)
    {
        return;
    }

    uint8_t *pbuffer = bc_radio_id_from_buffer(buffer + 1, &for_id);

    if (for_id != bc_radio_get_my_id())
    {
        return;
    }

    if (buffer[0] == BC_RADIO_HEADER_NODE_STATE_SET)
    {

        bool *state = NULL;

        bc_radio_bool_from_buffer(pbuffer + 1, &state);

        bc_radio_node_on_state_set(&for_id, pbuffer[0], state);
    }
    else if (buffer[0] == BC_RADIO_HEADER_NODE_STATE_GET)
    {
        bc_radio_node_on_state_get(&for_id, pbuffer[0]);
    }
    else if (buffer[0] == BC_RADIO_HEADER_NODE_BUFFER)
    {
        bc_radio_node_on_buffer(id, pbuffer, length - 1 - BC_RADIO_ID_SIZE);
    }
}
