#include <skyrc-wifi.h>

#include <json-c/json.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>


#define MAX_BUF  2048
#define PORT     8888

struct skyrc_current_state {
    uint8_t work_mode;
    bool is_working;
    uint8_t battery_index;
    uint8_t cell_count;
    uint8_t mode_index;
    uint16_t max_charge_current;
    uint16_t max_discharge_current;
};

struct skyrc_system_info {
    bool key_beep;
    bool system_beep;
    bool capacity_enabled;
    uint16_t capacity_mah;
    bool time_protection_enabled;
    uint16_t time_protection_minutes;
    uint8_t rest_time_minutes;
    uint8_t temperature;
    bool temperature_protection_enabled;
    uint8_t dc_setting;
    uint8_t ac_setting;
    uint8_t ni_mh_peak;
    uint8_t ni_cd_peak;
    uint16_t cell_voltage[6];
};

struct skyrc_device {
    enum skyrc_device_type device;
    int socket;
    struct sockaddr_storage servaddr;
    socklen_t servaddr_len;
    skyrc_current_state current_state[2];
    bool current_state_valid[2];
    skyrc_system_info system_info[2];
    bool system_info_valid[2];
    char *ip;
    char *id;
    char *name;
    char *mac;
    char *ver;
    char *password;
    char *mode;
};

static bool skyrc_same_endpoint(const struct sockaddr_storage *left, socklen_t left_size,
                               const struct sockaddr_storage *right, socklen_t right_size)
{
    if (!left || !right || left_size < sizeof(sa_family_t) || right_size < sizeof(sa_family_t) ||
        left->ss_family != right->ss_family)
        return false;
    if (left->ss_family == AF_INET && left_size >= sizeof(struct sockaddr_in) &&
        right_size >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)left;
        const struct sockaddr_in *b = (const struct sockaddr_in *)right;
        return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
    }
    if (left->ss_family == AF_INET6 && left_size >= sizeof(struct sockaddr_in6) &&
        right_size >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)left;
        const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)right;
        return a->sin6_port == b->sin6_port && a->sin6_scope_id == b->sin6_scope_id &&
               memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
    }
    return false;
}

struct skyrc_operations {
    const char **battery_type;
    const char ***mode;
};

struct skyrc_real_data_a {
    uint8_t mode;
    uint16_t capacity;
    uint16_t time;
    float voltage;
    float current;
    uint8_t ext_temp;
    uint8_t int_temp;
    uint16_t cells[6];
    uint8_t error_code[2];
};

#define SKYRC_SEND_RECEIVE_UDP(command, reply)                                                                                     \
    do {                                                                                                                          \
        if (sendto(device->socket, command, sizeof(command), 0, (struct sockaddr*)&device->servaddr, device->servaddr_len) < 0) { \
            return SKYRC_UNABLE_TO_SEND_MESSAGE;                                                                                   \
        }                                                                                                                         \
        struct sockaddr_storage sender_addr;                                                                                      \
        socklen_t sender_addr_size = sizeof(sender_addr);                                                                          \
        recieved = recvfrom(device->socket, reply, sizeof(reply), 0, (struct sockaddr*)&sender_addr, &sender_addr_size);          \
        if (recieved < 0) {                                                                                                       \
            return SKYRC_UNABLE_TO_RECIEVE_MESSAGE;                                                                                \
        }                                                                                                                         \
        reply_size = recieved;                                                                                                    \
    } while (0)


static const skyrc_operations skyrc_operation_b6 = {
    .battery_type = (const char*[]){"LiPo", "LiIo", "LiFe", "LiHV", "NiMH", "NiCd", "Pb", nullptr},
    .mode = (const char**[]) {
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance Charge", nullptr},
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", nullptr},
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", nullptr},
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", nullptr},
        (const char*[]){"Charge", "Auto Charge", "Discharge", "Re-Peak", "Cycle", nullptr},
        (const char*[]){"Charge", "Auto Charge", "Discharge", "Re-Peak", "Cycle", nullptr},
        (const char*[]){"Charge", "Discharge", nullptr},
        nullptr
    }
};

static const skyrc_operations skyrc_operation_w1000 = {
    .battery_type = (const char*[]){"LiPo", "LiIo", "LiFe", "LiHV", "NiMH", "NiCd", "Pb", nullptr},
    .mode = (const char**[]) {
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", "Micro Chg", "Micro Store", nullptr},
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", "Micro Chg", "Micro Store", nullptr},
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", "Micro Chg", "Micro Store", nullptr},
        (const char*[]){"Charge", "Discharge", "Storage", "Fast CHG", "Balance", "Micro Chg", "Micro Store", nullptr},
        (const char*[]){"Charge", "Auto Charge", "Discharge", "Re-Peak", "Cycle", nullptr},
        (const char*[]){"Charge", "Auto Charge", "Discharge", "Re-Peak", "Cycle", nullptr},
        (const char*[]){"Charge", "Discharge", nullptr},
        nullptr
    }
};

static const skyrc_operations skyrc_operation_dx00 = {
    .battery_type = (const char*[]){"LiPo", "LiFe", "LiIo", "LiHV", "NiMH", "NiCd", "Pb", nullptr},
    .mode = (const char**[]) {
        (const char*[]){"Charge", "Fast CHG", "Storage", "Discharge", "Balance", nullptr},
        (const char*[]){"Charge", "Fast CHG", "Storage", "Discharge", "Balance Charge", nullptr},
        (const char*[]){"Charge", "Fast CHG", "Storage", "Discharge", "Balance Charge", nullptr},
        (const char*[]){"Charge", "Fast CHG", "Storage", "Discharge", "Balance Charge", nullptr},
        (const char*[]){"Charge", "AUTO", "Discharge", "Re-peak", "Cycle", nullptr},
        (const char*[]){"Charge", "AUTO", "Discharge", "Re-peak", "Cycle", nullptr},
        (const char*[]){"Charge", "Discharge", nullptr},
        nullptr
    }
};



int skyrc_start_listen(int timeout)
{
    int sockfd;
    struct sockaddr_in servaddr;

    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        return SKYRC_FAIL_TO_CREATE_SOCKET;
    }

    // Filling server information
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        skyrc_stop_listen(sockfd);
        return SKYRC_FAIL_TO_BIND_TO_SOCKET;
    }

    struct timeval tv = {
        .tv_sec = timeout / 1000,
        .tv_usec = (timeout % 1000) * 1000
    };

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        skyrc_stop_listen(sockfd);
        return SKYRC_SETSOCKOPT_FAILED;
    }

    return sockfd;
}

enum skyrc_error skyrc_stop_listen(int socket)
{
    enum skyrc_error ret = SKYRC_NO_ERROR;

    int res = close(socket);
    if (res == -1)
    {
        ret = SKYRC_FAILED_TO_CLOSE_SOCKET;
    }

    return ret;
}

enum skyrc_error skyrc_send_broadcast(int sockfd)
{
    static const uint8_t payload[1] = {0};
    struct sockaddr_in servaddr;
    ssize_t sent;

    if (sockfd < 0)
        return SKYRC_INVALID_SOCKET;

    int broadcastEnable=1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        return SKYRC_SETSOCKOPT_FAILED;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sent = sendto(sockfd, payload, sizeof(payload), 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
    if (sent < 0)
    {
        return SKYRC_SENDTO_FAILED;
    }
    if ((size_t)sent != sizeof(payload))
        return SKYRC_UNABLE_TO_SEND_MESSAGE;

    return 0;
}

static int skyrc_hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Matches java.net.URLDecoder.decode() for the discovery response payload. */
static char *skyrc_url_decode(const unsigned char *input, size_t input_size, size_t *output_size)
{
    char *output = malloc(input_size + 1);
    size_t in = 0, out = 0;

    if (!output)
        return nullptr;

    while (in < input_size) {
        unsigned char c = input[in++];
        if (c == '+') {
            output[out++] = ' ';
        } else if (c == '%') {
            int high, low;
            if (in + 1 >= input_size ||
                (high = skyrc_hex_value(input[in])) < 0 ||
                (low = skyrc_hex_value(input[in + 1])) < 0) {
                free(output);
                return nullptr;
            }
            c = (unsigned char)((high << 4) | low);
            in += 2;
            if (c == '\0') {
                free(output);
                return nullptr;
            }
            output[out++] = (char)c;
        } else {
            output[out++] = (char)c;
        }
    }

    output[out] = '\0';
    *output_size = out;
    return output;
}

static json_object *skyrc_json_member(json_object *object, const char *name)
{
    json_object *value = nullptr;
    return json_object_object_get_ex(object, name, &value) ? value : nullptr;
}

skyrc_device *skyrc_find_device(int sockfd)
{
    skyrc_device *ret = nullptr;
    json_object *jobj = nullptr;
    json_object *data = nullptr;
    json_object *response = nullptr;
    json_tokener *tokener = nullptr;
    struct sockaddr_in cliaddr;
    unsigned char buffer[MAX_BUF + 1];
    char ip[INET_ADDRSTRLEN];
    size_t decoded_size = 0;
    char *decoded = nullptr;
    socklen_t addr_size = sizeof(cliaddr);
    ssize_t n;
    const char *id, *name, *mac, *version, *password, *mode;
    size_t parse_end;

    if (sockfd < 0)
        return nullptr;
    memset(&cliaddr, 0, sizeof(cliaddr));
    n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&cliaddr, &addr_size);
    if (n <= 1 || n > MAX_BUF || cliaddr.sin_family != AF_INET ||
        !inet_ntop(AF_INET, &cliaddr.sin_addr, ip, sizeof(ip)))
        goto cleanup;

    /* Discovery replies have a one-byte binary prefix followed by encoded JSON. */
    if (buffer[0] != 0)
        goto cleanup;
    decoded = skyrc_url_decode(buffer + 1, (size_t)n - 1, &decoded_size);
    if (!decoded || decoded_size == 0 || decoded_size > INT32_MAX)
        goto cleanup;

    tokener = json_tokener_new();
    if (!tokener)
        goto cleanup;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    jobj = json_tokener_parse_ex(tokener, decoded, (int)decoded_size);
    if (json_tokener_get_error(tokener) != json_tokener_success ||
        !jobj || json_object_get_type(jobj) != json_type_object)
        goto cleanup;
    parse_end = json_tokener_get_parse_end(tokener);
    while (parse_end < decoded_size && isspace((unsigned char)decoded[parse_end]))
        parse_end++;
    if (parse_end != decoded_size)
        goto cleanup;

    response = skyrc_json_member(jobj, "response");
    if (!response || (json_object_get_type(response) != json_type_int &&
                      json_object_get_type(response) != json_type_string))
        goto cleanup;
    if (json_object_get_type(response) == json_type_int) {
        if (json_object_get_int64(response) != 0)
            goto cleanup;
    } else {
        const char *response_str = json_object_get_string(response);
        if (!response_str || (strcmp(response_str, "0") != 0 && strcmp(response_str, "00") != 0))
            goto cleanup;
    }

    data = skyrc_json_member(jobj, "data");
    if (!data || json_object_get_type(data) != json_type_object)
        goto cleanup;
    json_object *id_obj = skyrc_json_member(data, "id");
    json_object *name_obj = skyrc_json_member(data, "name");
    json_object *mac_obj = skyrc_json_member(data, "mac");
    json_object *version_obj = skyrc_json_member(data, "ver");
    json_object *password_obj = skyrc_json_member(data, "password");
    json_object *mode_obj = skyrc_json_member(data, "mode");
    if (!id_obj || !name_obj || !mac_obj || !version_obj || !password_obj || !mode_obj ||
        json_object_get_type(id_obj) != json_type_string ||
        json_object_get_type(name_obj) != json_type_string ||
        json_object_get_type(mac_obj) != json_type_string ||
        json_object_get_type(version_obj) != json_type_string ||
        json_object_get_type(password_obj) != json_type_string ||
        (json_object_get_type(mode_obj) != json_type_int && json_object_get_type(mode_obj) != json_type_string))
        goto cleanup;

    id = json_object_get_string(id_obj);
    name = json_object_get_string(name_obj);
    mac = json_object_get_string(mac_obj);
    version = json_object_get_string(version_obj);
    password = json_object_get_string(password_obj);
    mode = json_object_get_string(mode_obj);
    if (!id || strlen(id) < 12 || !name || !mac || !version || !password || !mode)
        goto cleanup;

    ret = calloc(1, sizeof(*ret));
    if (!ret)
        goto cleanup;
    ret->socket = -1;
    if      (strncmp(id, "313030303839", 12) == 0) ret->device = SKYRC_D100;
    else if (strncmp(id, "313030303937", 12) == 0) ret->device = SKYRC_D200;
    else if (strncmp(id, "313030303834", 12) == 0) ret->device = SKYRC_B6MINI;
    else if (strncmp(id, "313030303038", 12) == 0) ret->device = SKYRC_B6AC;
    else if (strncmp(id, "313030303039", 12) == 0) ret->device = SKYRC_B6ACADD;
    else if (strncmp(id, "313030303639", 12) == 0) ret->device = SKYRC_W1000;
    else ret->device = SKYRC_UNKNOWN;

    ret->ip = strdup(ip);
    ret->id = strdup(id);
    ret->name = strdup(name);
    ret->mac = strdup(mac);
    ret->ver = strdup(version);
    ret->password = strdup(password);
    ret->mode = strdup(mode);
    if (!ret->ip || !ret->id || !ret->name || !ret->mac || !ret->ver || !ret->password || !ret->mode) {
        skyrc_free_item(ret);
        ret = nullptr;
    }

cleanup:
    if (jobj) json_object_put(jobj);
    if (tokener) json_tokener_free(tokener);
    free(decoded);
    return ret;
}

void skyrc_free_item(skyrc_device *item)
{
    if (item)
    {
        if (item->socket >= 0) close(item->socket);
        if (item->ip) free(item->ip);
        if (item->id) free(item->id);
        if (item->name) free(item->name);
        if (item->mac) free(item->mac);
        if (item->ver) free(item->ver);
        if (item->password) free(item->password);
        if (item->mode) free(item->mode);
        free(item);
    }
}

bool skyrc_open_device(const char *host, skyrc_device **device)
{
    struct addrinfo hints = {0};
    struct addrinfo *addresses = nullptr;
    bool connected = false;

    if (!host || !device)
        return false;
    *device = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, "8888", &hints, &addresses) != 0)
        return false;
    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
        char ip[NI_MAXHOST];
        skyrc_device *candidate = calloc(1, sizeof(*candidate));
        if (!candidate) break;
        candidate->socket = -1;
        candidate->device = SKYRC_UNKNOWN;
        if (getnameinfo(address->ai_addr, (socklen_t)address->ai_addrlen,
                        ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST) == 0)
            candidate->ip = strdup(ip);
        if (candidate->ip && address->ai_addrlen <= sizeof(candidate->servaddr)) {
            memcpy(&candidate->servaddr, address->ai_addr, address->ai_addrlen);
            candidate->servaddr_len = (socklen_t)address->ai_addrlen;
            candidate->socket = socket(address->ai_family, SOCK_DGRAM, IPPROTO_UDP);
        }
        if (candidate->socket >= 0) {
            *device = candidate;
            connected = true;
            break;
        }
        skyrc_free_item(candidate);
    }
    freeaddrinfo(addresses);
    return connected;
}

enum skyrc_device_type skyrc_device_get_type(const skyrc_device *device)
{
    return device ? device->device : SKYRC_UNKNOWN;
}

void skyrc_device_set_type(skyrc_device *device, enum skyrc_device_type type)
{
    if (device)
        device->device = type;
}

const char *skyrc_device_get_ip(const skyrc_device *device) { return device ? device->ip : NULL; }
const char *skyrc_device_get_id(const skyrc_device *device) { return device ? device->id : NULL; }
const char *skyrc_device_get_name(const skyrc_device *device) { return device ? device->name : NULL; }
const char *skyrc_device_get_mac(const skyrc_device *device) { return device ? device->mac : NULL; }
const char *skyrc_device_get_version(const skyrc_device *device) { return device ? device->ver : NULL; }
const char *skyrc_device_get_password(const skyrc_device *device) { return device ? device->password : NULL; }
const char *skyrc_device_get_mode(const skyrc_device *device) { return device ? device->mode : NULL; }

bool skyrc_device_is_initialized(const skyrc_device *device)
{
    return device && device->socket >= 0 && device->servaddr_len != 0;
}

const skyrc_operations *skyrc_get_operations(enum skyrc_device_type device_type)
{
    switch(device_type)
    {
    case SKYRC_D100:
    case SKYRC_D200:
        return &skyrc_operation_dx00;
    case SKYRC_W1000:
        return &skyrc_operation_w1000;
    case SKYRC_B6MINI:
    case SKYRC_B6AC:
    case SKYRC_B6ACADD:
        return &skyrc_operation_b6;
    default:;
    }

    return nullptr;
}

size_t skyrc_operations_get_battery_type_count(const skyrc_operations *operations)
{
    size_t count = 0;
    if (!operations || !operations->battery_type)
        return 0;
    while (operations->battery_type[count])
        count++;
    return count;
}

const char *skyrc_operations_get_battery_type(const skyrc_operations *operations, size_t battery_index)
{
    if (battery_index >= skyrc_operations_get_battery_type_count(operations))
        return nullptr;
    return operations->battery_type[battery_index];
}

size_t skyrc_operations_get_mode_count(const skyrc_operations *operations, size_t battery_index)
{
    size_t count = 0;
    if (!operations || !operations->mode ||
        battery_index >= skyrc_operations_get_battery_type_count(operations) ||
        !operations->mode[battery_index])
        return 0;
    while (operations->mode[battery_index][count])
        count++;
    return count;
}

const char *skyrc_operations_get_mode(const skyrc_operations *operations, size_t battery_index, size_t mode_index)
{
    if (mode_index >= skyrc_operations_get_mode_count(operations, battery_index))
        return nullptr;
    return operations->mode[battery_index][mode_index];
}

skyrc_real_data_a *skyrc_real_data_a_create(void)
{
    return calloc(1, sizeof(skyrc_real_data_a));
}

void skyrc_real_data_a_free(skyrc_real_data_a *data)
{
    free(data);
}

uint8_t skyrc_real_data_a_get_mode(const skyrc_real_data_a *data) { return data ? data->mode : 0; }
uint16_t skyrc_real_data_a_get_capacity(const skyrc_real_data_a *data) { return data ? data->capacity : 0; }
uint16_t skyrc_real_data_a_get_time(const skyrc_real_data_a *data) { return data ? data->time : 0; }
float skyrc_real_data_a_get_voltage(const skyrc_real_data_a *data) { return data ? data->voltage : 0.0f; }
float skyrc_real_data_a_get_current(const skyrc_real_data_a *data) { return data ? data->current : 0.0f; }
uint8_t skyrc_real_data_a_get_ext_temp(const skyrc_real_data_a *data) { return data ? data->ext_temp : 0; }
uint8_t skyrc_real_data_a_get_int_temp(const skyrc_real_data_a *data) { return data ? data->int_temp : 0; }

bool skyrc_real_data_a_get_cell(const skyrc_real_data_a *data, size_t cell_index, uint16_t *value)
{
    if (!data || !value || cell_index >= 6)
        return false;
    *value = data->cells[cell_index];
    return true;
}

bool skyrc_real_data_a_get_error_code(const skyrc_real_data_a *data, size_t code_index, uint8_t *value)
{
    if (!data || !value || code_index >= 2)
        return false;
    *value = data->error_code[code_index];
    return true;
}

static enum skyrc_error skyrc_request_current_state_standard(skyrc_device *device)
{
    static const uint8_t command[] = {1, 15, 3, 95, 0, 95, 0xff, 0xff};
    uint8_t reply[2000] = {0};
    ssize_t reply_size = 0;
    ssize_t recieved;
    skyrc_current_state parsed = {0};

    SKYRC_SEND_RECEIVE_UDP(command, reply);
    if (reply_size < 11)
        return reply_size == 0 ? SKYRC_EMPTY_PACKET_RECIEVED : SKYRC_INVALID_PACKET_SIZE;
    if (reply[0] != 1)
        return SKYRC_INVALID_STATUS;
    if (reply[3] != 95)
        return SKYRC_WRONG_REPLY_COMMAND_ID;

    parsed.work_mode = reply[5];
    parsed.is_working = parsed.work_mode == 1;
    if (parsed.is_working) {
        parsed.battery_index = reply[6];
        parsed.cell_count = reply[7];
        parsed.mode_index = reply[8];
    }
    parsed.max_charge_current = (uint16_t)reply[9] * 100;
    parsed.max_discharge_current = (uint16_t)reply[10] * 100;
    device->current_state[SKYRC_STATE_A] = parsed;
    device->current_state_valid[SKYRC_STATE_A] = true;
    return SKYRC_NO_ERROR;
}

static enum skyrc_error skyrc_request_current_state_dx00(skyrc_device *device, enum skyrc_device_state state)
{
    static const uint8_t command_a[] = {1, 15, 4, 95, 0, 0, 95, 0xff, 0xff};
    static const uint8_t command_b[] = {1, 15, 4, 95, 0, 1, 96, 0xff, 0xff};
    uint8_t reply[2000] = {0};
    ssize_t reply_size = 0;
    ssize_t recieved;
    skyrc_current_state parsed = {0};

    if (state == SKYRC_STATE_A)
        SKYRC_SEND_RECEIVE_UDP(command_a, reply);
    else
        SKYRC_SEND_RECEIVE_UDP(command_b, reply);
    if (reply_size < 30)
        return reply_size == 0 ? SKYRC_EMPTY_PACKET_RECIEVED : SKYRC_INVALID_PACKET_SIZE;
    if (reply[0] != 1)
        return SKYRC_INVALID_STATUS;
    if (reply[1] != 95 || reply[2] != (uint8_t)state)
        return SKYRC_WRONG_REPLY_COMMAND_ID;

    parsed.work_mode = reply[3];
    parsed.is_working = parsed.work_mode == 1 || parsed.work_mode == 2;
    parsed.battery_index = reply[16];
    if (parsed.is_working) {
        parsed.mode_index = reply[17];
        parsed.cell_count = reply[18];
    }
    parsed.max_charge_current = (uint16_t)reply[28] * 100;
    parsed.max_discharge_current = (uint16_t)reply[29] * 100;
    device->current_state[state] = parsed;
    device->current_state_valid[state] = true;
    return SKYRC_NO_ERROR;
}

enum skyrc_error skyrc_request_current_state(skyrc_device *device, enum skyrc_device_state state)
{
    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (state != SKYRC_STATE_A && state != SKYRC_STATE_B)
        return SKYRC_INVALID_INPUT_PARAMETER;
    if (device->socket < 0) return SKYRC_INVALID_SOCKET;

    if (device->device == SKYRC_D100 || device->device == SKYRC_D200)
        return skyrc_request_current_state_dx00(device, state);
    if (state != SKYRC_STATE_A)
        return SKYRC_INVALID_INPUT_PARAMETER;
    if (device->device == SKYRC_UNKNOWN)
        return SKYRC_UNKNOWN_DEVICE;
    return skyrc_request_current_state_standard(device);
}

bool skyrc_device_get_current_state(const skyrc_device *device, enum skyrc_device_state state, skyrc_current_state *result)
{
    if (!device || !result || (state != SKYRC_STATE_A && state != SKYRC_STATE_B) ||
        !device->current_state_valid[state])
        return false;
    *result = device->current_state[state];
    return true;
}

skyrc_current_state *skyrc_current_state_create(void)
{
    return calloc(1, sizeof(skyrc_current_state));
}

void skyrc_current_state_free(skyrc_current_state *state)
{
    free(state);
}

uint8_t skyrc_current_state_get_work_mode(const skyrc_current_state *state) { return state ? state->work_mode : 0; }
bool skyrc_current_state_is_working(const skyrc_current_state *state) { return state ? state->is_working : false; }
uint8_t skyrc_current_state_get_battery_index(const skyrc_current_state *state) { return state ? state->battery_index : 0; }
uint8_t skyrc_current_state_get_cell_count(const skyrc_current_state *state) { return state ? state->cell_count : 0; }
uint8_t skyrc_current_state_get_mode_index(const skyrc_current_state *state) { return state ? state->mode_index : 0; }
uint16_t skyrc_current_state_get_max_charge_current(const skyrc_current_state *state) { return state ? state->max_charge_current : 0; }
uint16_t skyrc_current_state_get_max_discharge_current(const skyrc_current_state *state) { return state ? state->max_discharge_current : 0; }

static uint16_t skyrc_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static enum skyrc_error skyrc_parse_system_info(const uint8_t *reply, size_t reply_size,
                                   bool is_dx00, enum skyrc_device_state state,
                                   skyrc_system_info *result)
{
    skyrc_system_info parsed = {0};

    if (reply_size == 0)
        return SKYRC_EMPTY_PACKET_RECIEVED;
    if (reply[0] != 1)
        return SKYRC_INVALID_STATUS;

    if (is_dx00) {
        if (reply_size < 19)
            return SKYRC_INVALID_PACKET_SIZE;
        if (reply[1] != 90 || reply[2] != (uint8_t)state)
            return SKYRC_WRONG_REPLY_COMMAND_ID;

        parsed.key_beep = reply[3] == 1;
        parsed.system_beep = reply[4] == 1;
        parsed.capacity_enabled = reply[5] == 1;
        parsed.time_protection_enabled = reply[6] == 1;
        parsed.capacity_mah = skyrc_read_be16(reply + 8);
        parsed.time_protection_minutes = skyrc_read_be16(reply + 10);
        parsed.temperature = reply[12];
        parsed.rest_time_minutes = reply[13];
        parsed.dc_setting = reply[14];
        parsed.ni_mh_peak = reply[15];
        parsed.ni_cd_peak = reply[16];
        parsed.ac_setting = reply[17];
        parsed.temperature_protection_enabled = reply[18] == 1;
    } else {
        if (reply_size < 33)
            return SKYRC_INVALID_PACKET_SIZE;
        if (reply[3] != 90)
            return SKYRC_WRONG_REPLY_COMMAND_ID;

        parsed.rest_time_minutes = reply[5];
        parsed.time_protection_enabled = reply[6] == 1;
        parsed.time_protection_minutes = skyrc_read_be16(reply + 7);
        parsed.capacity_enabled = reply[9] == 1;
        parsed.capacity_mah = skyrc_read_be16(reply + 10);
        parsed.key_beep = reply[12] == 1;
        parsed.system_beep = reply[13] == 1;
        parsed.temperature = reply[18];
        for (size_t cell = 0; cell < 6; ++cell) {
            uint16_t voltage = skyrc_read_be16(reply + 21 + cell * 2);
            parsed.cell_voltage[cell] = voltage < 2000 ? 0 : voltage;
        }
    }

    *result = parsed;
    return SKYRC_NO_ERROR;
}

static enum skyrc_error skyrc_request_system_info(skyrc_device *device, enum skyrc_device_state state)
{
    static const uint8_t command[] = {1, 15, 3, 90, 0, 90, 0xff, 0xff};
    static const uint8_t command_a[] = {1, 15, 4, 90, 0, 0, 90, 0xff, 0xff};
    static const uint8_t command_b[] = {1, 15, 4, 90, 0, 1, 91, 0xff, 0xff};
    uint8_t reply[2000] = {0};
    ssize_t reply_size = 0;
    ssize_t recieved;
    bool is_dx00 = device->device == SKYRC_D100 || device->device == SKYRC_D200;
    skyrc_system_info parsed = {0};
    enum skyrc_error status;

    if (device->socket < 0) return SKYRC_INVALID_SOCKET;
    if (state != SKYRC_STATE_A && state != SKYRC_STATE_B)
        return SKYRC_INVALID_INPUT_PARAMETER;

    if (is_dx00) {
        if (state == SKYRC_STATE_A)
            SKYRC_SEND_RECEIVE_UDP(command_a, reply);
        else
            SKYRC_SEND_RECEIVE_UDP(command_b, reply);
    } else {
        if (state != SKYRC_STATE_A)
            return SKYRC_INVALID_INPUT_PARAMETER;
        SKYRC_SEND_RECEIVE_UDP(command, reply);
    }

    status = skyrc_parse_system_info(reply, (size_t)reply_size, is_dx00, state, &parsed);
    if (status != SKYRC_NO_ERROR)
        return status;
    device->system_info[state] = parsed;
    device->system_info_valid[state] = true;
    return SKYRC_NO_ERROR;
}

enum skyrc_error skyrc_request_system_info_standard(skyrc_device *device)
{
    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (device->device == SKYRC_D100 || device->device == SKYRC_D200)
        return SKYRC_UNSUPPORTED_COMMAND;
    if (device->device == SKYRC_UNKNOWN)
        return SKYRC_UNKNOWN_DEVICE;
    return skyrc_request_system_info(device, SKYRC_STATE_A);
}

enum skyrc_error skyrc_request_system_info_dx00_a(skyrc_device *device)
{
    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (device->device != SKYRC_D100 && device->device != SKYRC_D200)
        return SKYRC_UNSUPPORTED_COMMAND;
    return skyrc_request_system_info(device, SKYRC_STATE_A);
}

enum skyrc_error skyrc_request_system_info_dx00_b(skyrc_device *device)
{
    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (device->device != SKYRC_D100 && device->device != SKYRC_D200)
        return SKYRC_UNSUPPORTED_COMMAND;
    return skyrc_request_system_info(device, SKYRC_STATE_B);
}

bool skyrc_device_get_system_info(const skyrc_device *device, enum skyrc_device_state state, skyrc_system_info *result)
{
    if (!device || !result || (state != SKYRC_STATE_A && state != SKYRC_STATE_B) ||
        !device->system_info_valid[state])
        return false;
    *result = device->system_info[state];
    return true;
}

skyrc_system_info *skyrc_system_info_create(void)
{
    return calloc(1, sizeof(skyrc_system_info));
}

void skyrc_system_info_free(skyrc_system_info *info)
{
    free(info);
}

bool skyrc_system_info_get_key_beep(const skyrc_system_info *info) { return info ? info->key_beep : false; }
bool skyrc_system_info_get_system_beep(const skyrc_system_info *info) { return info ? info->system_beep : false; }
bool skyrc_system_info_get_capacity_enabled(const skyrc_system_info *info) { return info ? info->capacity_enabled : false; }
uint16_t skyrc_system_info_get_capacity_mah(const skyrc_system_info *info) { return info ? info->capacity_mah : 0; }
bool skyrc_system_info_get_time_protection_enabled(const skyrc_system_info *info) { return info ? info->time_protection_enabled : false; }
uint16_t skyrc_system_info_get_time_protection_minutes(const skyrc_system_info *info) { return info ? info->time_protection_minutes : 0; }
uint8_t skyrc_system_info_get_rest_time_minutes(const skyrc_system_info *info) { return info ? info->rest_time_minutes : 0; }
uint8_t skyrc_system_info_get_temperature(const skyrc_system_info *info) { return info ? info->temperature : 0; }
bool skyrc_system_info_get_temperature_protection_enabled(const skyrc_system_info *info) { return info ? info->temperature_protection_enabled : false; }
uint8_t skyrc_system_info_get_dc_setting(const skyrc_system_info *info) { return info ? info->dc_setting : 0; }
uint8_t skyrc_system_info_get_ac_setting(const skyrc_system_info *info) { return info ? info->ac_setting : 0; }
uint8_t skyrc_system_info_get_ni_mh_peak(const skyrc_system_info *info) { return info ? info->ni_mh_peak : 0; }
uint8_t skyrc_system_info_get_ni_cd_peak(const skyrc_system_info *info) { return info ? info->ni_cd_peak : 0; }

bool skyrc_system_info_get_cell_voltage(const skyrc_system_info *info, size_t cell_index, uint16_t *millivolts)
{
    if (!info || !millivolts || cell_index >= 6)
        return false;
    *millivolts = info->cell_voltage[cell_index];
    return true;
}

static enum skyrc_error skyrc_parse_real_data(const uint8_t *reply, size_t reply_size,
                                 bool is_dx00, enum skyrc_device_state state,
                                 skyrc_real_data_a *result)
{
    skyrc_real_data_a parsed = {0};
    uint8_t work_mode;
    size_t capacity_offset, time_offset, voltage_offset, current_offset;
    size_t int_temp_offset, ext_temp_offset, cells_offset;
    bool has_measurements;

    if (reply_size == 0)
        return SKYRC_EMPTY_PACKET_RECIEVED;
    if (reply_size < 6)
        return SKYRC_INVALID_PACKET_SIZE;
    if (reply[0] != 1)
        return SKYRC_INVALID_STATUS;
    if (is_dx00) {
        if (reply[1] != 85 || reply[2] != (uint8_t)state)
            return SKYRC_WRONG_REPLY_COMMAND_ID;
        /* D100/D200 include the channel selector before their state fields. */
        capacity_offset = 12;
        time_offset = 6;
        voltage_offset = 8;
        current_offset = 10;
        int_temp_offset = 14;
        ext_temp_offset = 15;
        cells_offset = 18;
    } else {
        if (reply[3] != 85)
            return SKYRC_WRONG_REPLY_COMMAND_ID;
        capacity_offset = 6;
        time_offset = 8;
        voltage_offset = 10;
        current_offset = 12;
        ext_temp_offset = 14;
        int_temp_offset = 15;
        cells_offset = 18;
    }

    work_mode = reply[5];
    parsed.mode = work_mode;
    has_measurements = is_dx00 ? (work_mode == 1 || work_mode == 2)
                               : (work_mode == 0 || work_mode == 1);
    if (has_measurements) {
        if (reply_size < cells_offset + 12)
            return SKYRC_INVALID_PACKET_SIZE;
        parsed.capacity = skyrc_read_be16(reply + capacity_offset);
        parsed.time = skyrc_read_be16(reply + time_offset);
        parsed.voltage = skyrc_read_be16(reply + voltage_offset) / 1000.0f;
        parsed.current = skyrc_read_be16(reply + current_offset) / 1000.0f;
        parsed.int_temp = reply[int_temp_offset];
        parsed.ext_temp = reply[ext_temp_offset];
        for (size_t cell = 0; cell < 6; ++cell)
            parsed.cells[cell] = skyrc_read_be16(reply + cells_offset + cell * 2);
    } else if (is_dx00 && work_mode >= 128) {
        parsed.error_code[0] = work_mode;
    } else if (!is_dx00 && work_mode == 4) {
        if (reply_size < 8)
            return SKYRC_INVALID_PACKET_SIZE;
        parsed.error_code[0] = reply[6];
        parsed.error_code[1] = reply[7];
    }

    *result = parsed;
    return SKYRC_NO_ERROR;
}

enum skyrc_error skyrc_get_real_data(skyrc_device *device, enum skyrc_device_state state, skyrc_real_data_a *result)
{
    static const uint8_t command[] = {1, 15, 3, 85, 0, 85, 0xff, 0xff};
    static const uint8_t command_a[] = {1, 15, 4, 85, 0, 0, 85, 0xff, 0xff};
    static const uint8_t command_b[] = {1, 15, 4, 85, 0, 1, 86, 0xff, 0xff};
    uint8_t reply[2000] = {0};
    ssize_t reply_size = 0;
    ssize_t recieved;
    bool is_dx00;
    enum skyrc_error status;

    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (!result) return SKYRC_NULL_RESULT_HANDLE;
    if (state != SKYRC_STATE_A && state != SKYRC_STATE_B)
        return SKYRC_INVALID_INPUT_PARAMETER;
    if (device->socket < 0) return SKYRC_INVALID_SOCKET;

    is_dx00 = device->device == SKYRC_D100 || device->device == SKYRC_D200;
    if (is_dx00) {
        if (state == SKYRC_STATE_A)
            SKYRC_SEND_RECEIVE_UDP(command_a, reply);
        else
            SKYRC_SEND_RECEIVE_UDP(command_b, reply);
    } else {
        if (device->device == SKYRC_UNKNOWN)
            return SKYRC_UNKNOWN_DEVICE;
        if (state != SKYRC_STATE_A)
            return SKYRC_INVALID_INPUT_PARAMETER;
        SKYRC_SEND_RECEIVE_UDP(command, reply);
    }

    status = skyrc_parse_real_data(reply, (size_t)reply_size, is_dx00, state, result);
    return status;
}

static void skyrc_write_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

enum skyrc_error skyrc_start(skyrc_device *device,
                                enum skyrc_device_state channel,
                                uint8_t battery_index,
                                uint8_t cell_count,
                                uint8_t mode_index,
                                uint16_t charge_current,
                                uint16_t discharge_current,
                                uint16_t cutoff_voltage_per_cell,
                                uint16_t charge_voltage,
                                uint8_t charge_voltage_profile,
                                uint8_t repeat_peak,
                                uint8_t cycle_mode,
                                uint8_t cycle_count,
                                uint8_t rest_time_minutes,
                                uint16_t trickle_current)
{
    uint8_t request[27] = {1};
    uint8_t *program = request + 1;
    uint8_t reply[2000] = {0};
    struct sockaddr_storage sender_addr;
    socklen_t sender_addr_size = sizeof(sender_addr);
    ssize_t sent, received;
    unsigned int checksum = 0;
    bool is_dx00;
    const skyrc_operations *operations;
    uint16_t cutoff_voltage;

    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (device->socket < 0) return SKYRC_INVALID_SOCKET;
    is_dx00 = device->device == SKYRC_D100 || device->device == SKYRC_D200;
    operations = skyrc_get_operations(device->device);
    if (!operations) return SKYRC_UNKNOWN_DEVICE;
    if (channel != SKYRC_STATE_A && channel != SKYRC_STATE_B)
        return SKYRC_INVALID_INPUT_PARAMETER;
    if (!is_dx00 && channel != SKYRC_STATE_A)
        return SKYRC_INVALID_INPUT_PARAMETER;
    if (battery_index >= skyrc_operations_get_battery_type_count(operations) ||
        mode_index >= skyrc_operations_get_mode_count(operations, battery_index) ||
        cell_count == 0 || cell_count > (is_dx00 ? 15 : 20))
        return SKYRC_INVALID_INPUT_PARAMETER;

    if (is_dx00) {
        uint32_t total_cutoff = (uint32_t)cutoff_voltage_per_cell * cell_count;
        if (total_cutoff > UINT16_MAX) return SKYRC_INVALID_INPUT_PARAMETER;
        cutoff_voltage = (uint16_t)total_cutoff;
    } else {
        cutoff_voltage = cutoff_voltage_per_cell;
    }

    /* APK program body: start flag, packet size, command, then family data. */
    program[0] = 15;
    program[1] = 22;
    program[2] = 5;
    program[3] = 0;
    if (is_dx00) {
        program[4] = (uint8_t)channel;
        program[5] = battery_index;
        program[6] = cell_count;
        program[7] = mode_index;
        skyrc_write_be16(program + 8, charge_current);
        skyrc_write_be16(program + 10, discharge_current);
        program[12] = cycle_count;
        program[13] = rest_time_minutes;
        program[14] = cycle_mode;
        program[15] = charge_voltage_profile;
        skyrc_write_be16(program + 16, cutoff_voltage);
        skyrc_write_be16(program + 18, charge_voltage);
        skyrc_write_be16(program + 20, trickle_current);
        program[22] = repeat_peak;
    } else {
        program[4] = battery_index;
        program[5] = cell_count;
        program[6] = mode_index;
        skyrc_write_be16(program + 7, charge_current);
        skyrc_write_be16(program + 9, discharge_current);
        skyrc_write_be16(program + 11, cutoff_voltage);
        if (device->device == SKYRC_W1000 || battery_index != 6)
            skyrc_write_be16(program + 13, charge_voltage);
        if (battery_index == 4 || battery_index == 5) {
            if (mode_index == 3)
                program[15] = repeat_peak;
            else if (mode_index == 4) {
                program[15] = cycle_mode;
                program[16] = cycle_count;
            }
            program[17] = 0;
            program[18] = 100;
        }
    }
    for (size_t i = 2; i <= 22; ++i)
        checksum += program[i];
    program[23] = (uint8_t)checksum;
    program[24] = 0xff;
    program[25] = 0xff;

    sent = sendto(device->socket, request, sizeof(request), 0,
                  (struct sockaddr *)&device->servaddr, device->servaddr_len);
    if (sent < 0 || (size_t)sent != sizeof(request))
        return SKYRC_UNABLE_TO_SEND_MESSAGE;

    received = recvfrom(device->socket, reply, sizeof(reply), 0,
                        (struct sockaddr *)&sender_addr, &sender_addr_size);
    if (received < 0) return SKYRC_UNABLE_TO_RECIEVE_MESSAGE;
    if (received == 0) return SKYRC_EMPTY_PACKET_RECIEVED;
    if (!skyrc_same_endpoint(&sender_addr, sender_addr_size,
                             &device->servaddr, device->servaddr_len))
        return SKYRC_WRONG_REPLY_COMMAND_ID;
    if (reply[0] != 1)
        return SKYRC_INVALID_STATUS;
    if (received < (is_dx00 ? 2 : 4))
        return SKYRC_INVALID_PACKET_SIZE;

    return SKYRC_NO_ERROR;
}

enum skyrc_error skyrc_stop(skyrc_device *device, int channel)
{
    const uint8_t command[]          = {1, 15, 3, 0xfe, 0, 0xfe, 0xff, 0xff};
    const uint8_t command_dx00_ch1[] = {1, 15, 4, 0xfe, 0,  0, 0xfe, 0xff, 0xff};
    const uint8_t command_dx00_ch2[] = {1, 15, 4, 0xfe, 0,  1, 0xff, 0xff, 0xff};
    uint8_t reply[2000] = {0, };
    ssize_t reply_size = 0;
    ssize_t recieved = 0;
    bool is_dx00;

    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (device->socket < 0) return SKYRC_INVALID_SOCKET;

    is_dx00 = device->device == SKYRC_D100 || device->device == SKYRC_D200;
    if (device->device == SKYRC_UNKNOWN)
        return SKYRC_UNKNOWN_DEVICE;

    if (is_dx00) {
        switch(channel)
        {
        case 1:
            SKYRC_SEND_RECEIVE_UDP(command_dx00_ch1, reply);
            break;
        case 2:
            SKYRC_SEND_RECEIVE_UDP(command_dx00_ch2, reply);
            break;
        default:
            return SKYRC_INVALID_INPUT_PARAMETER;
        }
    } else {
        SKYRC_SEND_RECEIVE_UDP(command, reply);
    }

    if (reply_size == 0)
        return SKYRC_EMPTY_PACKET_RECIEVED;
    if (reply[0] != 1)
        return SKYRC_INVALID_STATUS;
    if (is_dx00) {
        if (reply_size < 3)
            return SKYRC_INVALID_PACKET_SIZE;
        if (reply[1] != 0xfe || reply[2] != (uint8_t)(channel - 1))
            return SKYRC_WRONG_REPLY_COMMAND_ID;
    } else {
        if (reply_size < 4)
            return SKYRC_INVALID_PACKET_SIZE;
        if (reply[3] != 0xfe)
            return SKYRC_WRONG_REPLY_COMMAND_ID;
    }

    return SKYRC_NO_ERROR;
}

static enum skyrc_error skyrc_send_system_option(skyrc_device *device, const uint8_t *command,
                                    size_t command_size)
{
    uint8_t reply[2000] = {0};
    struct sockaddr_storage sender_addr;
    socklen_t sender_addr_size = sizeof(sender_addr);
    ssize_t sent, received;
    bool is_dx00;

    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (!device->ip) return SKYRC_INVALID_INPUT_PARAMETER;
    if (device->socket < 0) return SKYRC_INVALID_SOCKET;
    if (device->device == SKYRC_UNKNOWN) return SKYRC_UNKNOWN_DEVICE;

    sent = sendto(device->socket, command, command_size, 0,
                  (struct sockaddr *)&device->servaddr, device->servaddr_len);
    if (sent < 0) return SKYRC_UNABLE_TO_SEND_MESSAGE;
    if ((size_t)sent != command_size) return SKYRC_UNABLE_TO_SEND_MESSAGE;

    received = recvfrom(device->socket, reply, sizeof(reply), 0,
                        (struct sockaddr *)&sender_addr, &sender_addr_size);
    if (received < 0) return SKYRC_UNABLE_TO_RECIEVE_MESSAGE;
    if (received == 0) return SKYRC_EMPTY_PACKET_RECIEVED;
    if (!skyrc_same_endpoint(&sender_addr, sender_addr_size,
                             &device->servaddr, device->servaddr_len))
        return SKYRC_WRONG_REPLY_COMMAND_ID;
    if (reply[0] != 1) return SKYRC_INVALID_STATUS;

    is_dx00 = device->device == SKYRC_D100 || device->device == SKYRC_D200;
    if (is_dx00) {
        if (received < 2) return SKYRC_INVALID_PACKET_SIZE;
        if (reply[1] != 17) return SKYRC_WRONG_REPLY_COMMAND_ID;
    } else {
        if (received < 4) return SKYRC_INVALID_PACKET_SIZE;
        if (reply[3] != 17) return SKYRC_WRONG_REPLY_COMMAND_ID;
    }

    return SKYRC_NO_ERROR;
}

enum skyrc_error skyrc_ext_set_speaker(skyrc_device *device, bool key, bool buzzer)
{
    uint8_t command[] = {1, 15, 7, 17, 3, 0, 0, 0, 0, 0, 0xff, 0xff};

    command[6] = key ? 1 : 0;
    command[7] = buzzer ? 1 : 0;

    for (int i = 3; i < 9; i++) {
        command[9] += command[i];
    }

    return skyrc_send_system_option(device, command, sizeof(command));
}

enum skyrc_error skyrc_ext_set_capacity(skyrc_device *device, bool on, uint16_t capacity)
{
    uint8_t command[] = {1, 15, 7, 17, 2, 0, 0, 0, 0, 0, 0xff, 0xff};
    command[6] = on ? 1 : 0;
    command[7] = (uint8_t)(capacity >> 8);
    command[8] = (uint8_t)(capacity >> 0);

    for (int i = 3; i < 9; i++) {
        command[9] += command[i];
    }

    return skyrc_send_system_option(device, command, sizeof(command));
}

enum skyrc_error skyrc_ext_set_rest_time(skyrc_device *device, uint8_t minutes)
{
    uint8_t command[] = {1, 15, 7, 17, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    command[6] = minutes;

    for (int i = 3; i < 9; i++) {
        command[9] += command[i];
    }

    return skyrc_send_system_option(device, command, sizeof(command));
}

enum skyrc_error skyrc_ext_set_time(skyrc_device *device, bool on, uint16_t minutes)
{
    uint8_t command[] = {1, 15, 7, 17, 1, 0, 0, 0, 0, 0, 0xff, 0xff};
    command[6] = on ? 1 : 0;
    command[7] = (uint8_t)(minutes >> 8);
    command[8] = (uint8_t)(minutes >> 0);

    for (int i = 3; i < 9; i++) {
        command[9] += command[i];
    }

    return skyrc_send_system_option(device, command, sizeof(command));
}

enum skyrc_error skyrc_ext_set_temp(skyrc_device *device, uint8_t temperature)
{
    uint8_t command[] = {1, 15, 7, 17, 5, 0, 0, 0, 0, 0, 0xff, 0xff};

    command[6] = temperature;

    for (int i = 3; i < 9; i++) {
        command[9] += command[i];
    }

    return skyrc_send_system_option(device, command, sizeof(command));
}

#define SKYRC_HTTP_MAX_RESPONSE 32768

static const char *skyrc_find_header_end(const char *data, size_t size)
{
    for (size_t i = 0; i + 3 < size; ++i)
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n')
            return data + i + 4;
    return nullptr;
}

static const char *skyrc_find_content_length(const char *headers)
{
    const char *line = strstr(headers, "\r\n");
    const char header_name[] = "Content-Length:";
    while (line) {
        line += 2;
        if (strncasecmp(line, header_name, sizeof(header_name) - 1) == 0)
            return line + sizeof(header_name) - 1;
        line = strstr(line, "\r\n");
    }
    return nullptr;
}

static enum skyrc_error skyrc_http_get(skyrc_device *device, const char *path, char **json_body)
{
    struct sockaddr_storage address;
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    char request[8192];
    char response[SKYRC_HTTP_MAX_RESPONSE + 1];
    size_t request_size, response_size = 0;
    const char *body;
    int http_socket = -1, http_status = 0;
    enum skyrc_error status = SKYRC_UNKNOWN_ERROR;
    ssize_t count;

    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (!path || !json_body) return SKYRC_INVALID_INPUT_PARAMETER;
    *json_body = nullptr;
    if (device->socket < 0 || device->servaddr_len == 0)
        return SKYRC_INVALID_SOCKET;
    if (device->device == SKYRC_UNKNOWN)
        return SKYRC_UNKNOWN_DEVICE;
    if (device->servaddr.ss_family != AF_INET && device->servaddr.ss_family != AF_INET6)
        return SKYRC_INVALID_INPUT_PARAMETER;

    http_socket = socket(device->servaddr.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (http_socket < 0) return SKYRC_FAIL_TO_CREATE_SOCKET;
    setsockopt(http_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(http_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    memcpy(&address, &device->servaddr, device->servaddr_len);
    if (address.ss_family == AF_INET)
        ((struct sockaddr_in *)&address)->sin_port = htons(80);
    else
        ((struct sockaddr_in6 *)&address)->sin6_port = htons(80);
    if (connect(http_socket, (struct sockaddr *)&address, device->servaddr_len) < 0) {
        close(http_socket);
        return SKYRC_UNABLE_TO_SEND_MESSAGE;
    }

    int request_length = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nConnection: close\r\n\r\n", path);
    if (request_length < 0 || (size_t)request_length >= sizeof(request)) {
        close(http_socket);
        return SKYRC_INVALID_INPUT_PARAMETER;
    }
    request_size = (size_t)request_length;
    for (size_t sent = 0; sent < request_size;) {
        count = send(http_socket, request + sent, request_size - sent, 0);
        if (count <= 0) {
            close(http_socket);
            return SKYRC_UNABLE_TO_SEND_MESSAGE;
        }
        sent += (size_t)count;
    }

    while (response_size < SKYRC_HTTP_MAX_RESPONSE) {
        count = recv(http_socket, response + response_size,
                     SKYRC_HTTP_MAX_RESPONSE - response_size, 0);
        if (count == 0) break;
        if (count < 0) {
            close(http_socket);
            return SKYRC_UNABLE_TO_RECIEVE_MESSAGE;
        }
        response_size += (size_t)count;
        response[response_size] = '\0';
        body = skyrc_find_header_end(response, response_size);
        if (body) {
            const char *length_header = skyrc_find_content_length(response);
            if (length_header) {
                char *end = nullptr;
                unsigned long expected = strtoul(length_header, &end, 10);
                size_t body_size = (size_t)(response + response_size - body);
                if (end != length_header && expected <= SKYRC_HTTP_MAX_RESPONSE &&
                    body_size >= expected)
                    break;
            }
        }
    }
    close(http_socket);
    response[response_size] = '\0';
    body = skyrc_find_header_end(response, response_size);
    if (!body || sscanf(response, "HTTP/%*u.%*u %d", &http_status) != 1 || http_status != 200)
        return SKYRC_INVALID_STATUS;
    size_t body_size = (size_t)(response + response_size - body);
    *json_body = malloc(body_size + 1);
    if (!*json_body) return SKYRC_UNKNOWN_ERROR;
    memcpy(*json_body, body, body_size);
    (*json_body)[body_size] = '\0';
    status = SKYRC_NO_ERROR;
    return status;
}

static json_object *skyrc_parse_http_json(const char *body)
{
    json_tokener *tokener;
    json_object *root;
    size_t body_size;
    size_t end;

    if (!body) return nullptr;
    body_size = strlen(body);
    if (body_size > INT32_MAX) return nullptr;
    tokener = json_tokener_new();
    if (!tokener) return nullptr;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    root = json_tokener_parse_ex(tokener, body, (int)body_size);
    end = json_tokener_get_parse_end(tokener);
    if (json_tokener_get_error(tokener) != json_tokener_success || !root ||
        json_object_get_type(root) != json_type_object || end != body_size) {
        if (root) json_object_put(root);
        root = nullptr;
    }
    json_tokener_free(tokener);
    return root;
}

static bool skyrc_http_code_is_success(json_object *root)
{
    json_object *code = skyrc_json_member(root, "code");
    return code && json_object_get_type(code) == json_type_int &&
           json_object_get_int(code) == 200;
}

enum skyrc_error skyrc_wifi_scan(skyrc_device *device, char *networks_json,
                    size_t json_capacity, size_t *json_size)
{
    char *body = nullptr;
    json_object *root = nullptr, *response = nullptr, *data = nullptr;
    const char *network_text;
    size_t needed;
    enum skyrc_error status;

    if (!networks_json || !json_size) return SKYRC_NULL_RESULT_HANDLE;
    *json_size = 0;
    status = skyrc_http_get(device, "/cmd=02", &body);
    if (status != SKYRC_NO_ERROR) return status;
    root = skyrc_parse_http_json(body);
    free(body);
    if (!root) return SKYRC_INVALID_PACKET_SIZE;
    response = skyrc_json_member(root, "response");
    data = skyrc_json_member(root, "data");
    if (!skyrc_http_code_is_success(root) || !response || !data ||
        json_object_get_type(response) != json_type_string ||
        json_object_get_type(data) != json_type_string ||
        strcmp(json_object_get_string(response), "01") == 0) {
        json_object_put(root);
        return SKYRC_INVALID_STATUS;
    }
    network_text = json_object_get_string(data);
    needed = strlen(network_text) + 1;
    *json_size = needed;
    if (needed > json_capacity) {
        json_object_put(root);
        return SKYRC_INVALID_PACKET_SIZE;
    }
    memcpy(networks_json, network_text, needed);
    json_object_put(root);
    return SKYRC_NO_ERROR;
}

static char *skyrc_form_url_encode(const char *input)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t input_size;
    char *output, *cursor;

    if (!input) return nullptr;
    input_size = strlen(input);
    if (input_size > (SIZE_MAX - 1) / 3) return nullptr;
    output = malloc(input_size * 3 + 1);
    if (!output) return nullptr;
    cursor = output;
    for (size_t i = 0; i < input_size; ++i) {
        unsigned char c = (unsigned char)input[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_') {
            *cursor++ = (char)c;
        } else if (c == ' ') {
            *cursor++ = '+';
        } else {
            *cursor++ = '%';
            *cursor++ = hex[c >> 4];
            *cursor++ = hex[c & 0x0f];
        }
    }
    *cursor = '\0';
    return output;
}

enum skyrc_error skyrc_wifi_configure(skyrc_device *device, const char *ssid,
                         const char *password, uint8_t security)
{
    json_object *configuration = nullptr, *root = nullptr;
    const char *json_text;
    char *encoded = nullptr, *path = nullptr, *body = nullptr;
    enum skyrc_error status;

    if (!device) return SKYRC_NULL_DEVICE_HANDLE;
    if (!ssid || !password) return SKYRC_INVALID_INPUT_PARAMETER;
    configuration = json_object_new_object();
    if (!configuration) return SKYRC_UNKNOWN_ERROR;
    json_object_object_add(configuration, "mode", json_object_new_int(1));
    json_object_object_add(configuration, "ssid", json_object_new_string(ssid));
    json_object_object_add(configuration, "password", json_object_new_string(password));
    json_object_object_add(configuration, "security", json_object_new_int(security));
    json_text = json_object_to_json_string_ext(configuration, JSON_C_TO_STRING_PLAIN);
    encoded = skyrc_form_url_encode(json_text);
    json_object_put(configuration);
    if (!encoded) return SKYRC_UNKNOWN_ERROR;
    {
        static const char prefix[] = "/cmd=01&json=";
        size_t prefix_len = strlen(prefix);
        size_t encoded_len = strlen(encoded);
        size_t path_size = prefix_len + encoded_len + 1;
        path = malloc(path_size);
        if (!path) {
            free(encoded);
            return SKYRC_UNKNOWN_ERROR;
        }
        memcpy(path, prefix, prefix_len);
        memcpy(path + prefix_len, encoded, encoded_len + 1);
    }
    free(encoded);

    status = skyrc_http_get(device, path, &body);
    free(path);
    if (status != SKYRC_NO_ERROR) return status;
    root = skyrc_parse_http_json(body);
    free(body);
    if (!root) return SKYRC_INVALID_PACKET_SIZE;
    status = skyrc_http_code_is_success(root) ? SKYRC_NO_ERROR : SKYRC_INVALID_STATUS;
    json_object_put(root);
    return status;
}
