/**
 * skyrc - command-line interface for libskyrc-wifi
 *
 * Subcommands:
 *   discover                        Broadcast and list all responding chargers.
 *   list-ops     <model>            List battery types and modes for a model.
 *   status       <host> [--model M] Print current program state.
 *   info         <host> [--model M] Print system configuration.
 *   monitor      <host> [--model M] Poll live measurements until Ctrl-C.
 *   start        <host> --model M --battery B --mode M --cells N
 *                       --charge-current A [options...]
 *   stop         <host> [--model M] [--channel 1|2]
 *   set-speaker  <host> [--model M] --key on|off --buzzer on|off
 *   set-capacity <host> [--model M] on|off [--limit mAh]
 *   set-time-protection <host> [--model M] on|off [--minutes N]
 *   set-rest-time <host> [--model M] --minutes N
 *   set-temp     <host> [--model M] --threshold N
 *   wifi-scan    <host> [--model M]
 *   wifi-connect <host> [--model M] --ssid S --password P --security N
 *
 * Model names: d100, d200, b6mini, b6ac, b6acadd, w1000
 * Currents are in amperes (e.g., 1.5).  Voltages are in volts (e.g., 4.20).
 * These are converted to the protocol's milliamp / millivolt units internally.
 */

#include <skyrc-wifi.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* ================================================================ helpers */

static const char *device_type_name(enum skyrc_device_type t)
{
    switch (t) {
    case SKYRC_D100:    return "D100";
    case SKYRC_D200:    return "D200";
    case SKYRC_B6MINI:  return "B6 Mini";
    case SKYRC_B6AC:    return "B6AC";
    case SKYRC_B6ACADD: return "B6AC Plus";
    case SKYRC_W1000:   return "W1000";
    default:            return "Unknown";
    }
}

static const char *skyrc_error_name(enum skyrc_error e)
{
    switch (e) {
    case SKYRC_NO_ERROR:                  return "no error";
    case SKYRC_NULL_DEVICE_HANDLE:        return "null device handle";
    case SKYRC_NULL_RESULT_HANDLE:        return "null result handle";
    case SKYRC_FAIL_TO_CREATE_SOCKET:     return "failed to create socket";
    case SKYRC_FAIL_TO_BIND_TO_SOCKET:    return "failed to bind socket";
    case SKYRC_SETSOCKOPT_FAILED:         return "setsockopt failed";
    case SKYRC_FAILED_TO_CLOSE_SOCKET:    return "failed to close socket";
    case SKYRC_INVALID_SOCKET:            return "invalid socket";
    case SKYRC_INVALID_INPUT_PARAMETER:   return "invalid input parameter";
    case SKYRC_SENDTO_FAILED:             return "sendto failed";
    case SKYRC_UNABLE_TO_SEND_MESSAGE:    return "unable to send message";
    case SKYRC_UNABLE_TO_RECIEVE_MESSAGE: return "unable to receive message";
    case SKYRC_NO_PACKET_RECIEVED:        return "no packet received";
    case SKYRC_EMPTY_PACKET_RECIEVED:     return "empty packet received";
    case SKYRC_INVALID_PACKET_SIZE:       return "invalid packet size";
    case SKYRC_INVALID_STATUS:            return "invalid status";
    case SKYRC_WRONG_REPLY_COMMAND_ID:    return "wrong reply command id";
    case SKYRC_UNKNOWN_DEVICE:            return "unknown device";
    case SKYRC_UNSUPPORTED_COMMAND:       return "unsupported command";
    default:                              return "unknown error";
    }
}

static enum skyrc_device_type parse_model(const char *s)
{
    if (!s) return SKYRC_UNKNOWN;
    if (strcasecmp(s, "d100")    == 0) return SKYRC_D100;
    if (strcasecmp(s, "d200")    == 0) return SKYRC_D200;
    if (strcasecmp(s, "b6mini")  == 0 ||
        strcasecmp(s, "b6-mini") == 0) return SKYRC_B6MINI;
    if (strcasecmp(s, "b6ac")    == 0) return SKYRC_B6AC;
    if (strcasecmp(s, "b6acadd") == 0 ||
        strcasecmp(s, "b6ac+")   == 0) return SKYRC_B6ACADD;
    if (strcasecmp(s, "w1000")   == 0) return SKYRC_W1000;
    return SKYRC_UNKNOWN;
}

/* ------ option parsing ------ */

/** Return the value of the first "--key value" pair, or NULL if not found. */
static const char *find_opt(int argc, char **argv, const char *key)
{
    for (int i = 0; i < argc - 1; ++i)
        if (strcmp(argv[i], key) == 0) return argv[i + 1];
    return NULL;
}

static bool parse_uint8(const char *s, uint8_t *out)
{
    if (!s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end || v > 255) return false;
    *out = (uint8_t)v;
    return true;
}

static bool parse_uint16(const char *s, uint16_t *out)
{
    if (!s) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end || v > 65535) return false;
    *out = (uint16_t)v;
    return true;
}

static bool parse_double(const char *s, double *out)
{
    if (!s) return false;
    char *end;
    *out = strtod(s, &end);
    return end != s && *end == '\0';
}

/** Parse amperes string → milliamps uint16_t. */
static bool amps_to_ma(const char *s, uint16_t *out)
{
    double f;
    if (!parse_double(s, &f) || f < 0.0 || f * 1000.0 > 65535.0) return false;
    *out = (uint16_t)(f * 1000.0 + 0.5);
    return true;
}

/** Parse volts string → millivolts uint16_t. */
static bool volts_to_mv(const char *s, uint16_t *out)
{
    double f;
    if (!parse_double(s, &f) || f < 0.0 || f * 1000.0 > 65535.0) return false;
    *out = (uint16_t)(f * 1000.0 + 0.5);
    return true;
}

/**
 * Open device and set model type from --model option (if provided).
 * Prints an error and returns NULL on connection failure.
 */
static skyrc_device *open_device_with_model(const char *host, int argc, char **argv)
{
    skyrc_device *dev = NULL;
    if (!skyrc_open_device(host, &dev)) {
        fprintf(stderr, "error: could not connect to %s\n", host);
        return NULL;
    }
    const char *ms = find_opt(argc, argv, "--model");
    if (ms) {
        enum skyrc_device_type t = parse_model(ms);
        if (t == SKYRC_UNKNOWN)
            fprintf(stderr, "warning: unknown model '%s', ignoring\n", ms);
        else
            skyrc_device_set_type(dev, t);
    }
    return dev;
}

/* ============================================================ cmd_discover */

static int cmd_discover(void)
{
    int sock = skyrc_start_listen(SKYRC_DEFAULT_TIMEOUT);
    if (sock <= 0) {
        fprintf(stderr, "discover: could not open listener\n");
        return 1;
    }

    enum skyrc_error err = skyrc_send_broadcast(sock);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "discover: broadcast failed: %s\n", skyrc_error_name(err));
        skyrc_stop_listen(sock);
        return 1;
    }

    printf("Scanning for chargers (%.1f s timeout)...\n",
           SKYRC_DEFAULT_TIMEOUT / 1000.0);

    int found = 0;
    for (;;) {
        skyrc_device *dev = skyrc_find_device(sock);
        if (!dev) break;
        ++found;
        const char *ip  = skyrc_device_get_ip(dev);
        const char *nm  = skyrc_device_get_name(dev);
        const char *id  = skyrc_device_get_id(dev);
        const char *mac = skyrc_device_get_mac(dev);
        const char *ver = skyrc_device_get_version(dev);
        const char *mod = skyrc_device_get_mode(dev);
        printf("\nCharger #%d\n", found);
        printf("  IP      : %s\n", ip  ? ip  : "(none)");
        printf("  Model   : %s\n", device_type_name(skyrc_device_get_type(dev)));
        printf("  Name    : %s\n", nm  ? nm  : "(none)");
        printf("  ID      : %s\n", id  ? id  : "(none)");
        printf("  MAC     : %s\n", mac ? mac : "(none)");
        printf("  Firmware: %s\n", ver ? ver : "(none)");
        printf("  Mode    : %s\n", mod ? mod : "(none)");
        skyrc_free_item(dev);
    }
    skyrc_stop_listen(sock);

    if (found == 0) { printf("No chargers found.\n"); return 1; }
    return 0;
}

/* ============================================================ cmd_list_ops */

static int cmd_list_ops(const char *model_str)
{
    enum skyrc_device_type t = parse_model(model_str);
    if (t == SKYRC_UNKNOWN) {
        fprintf(stderr,
                "list-ops: unknown model '%s'\n"
                "  Known models: d100, d200, b6mini, b6ac, b6acadd, w1000\n",
                model_str);
        return 1;
    }
    const skyrc_operations *ops = skyrc_get_operations(t);
    if (!ops) {
        fprintf(stderr, "list-ops: no operations table for model %s\n",
                device_type_name(t));
        return 1;
    }
    printf("Model: %s\n", device_type_name(t));
    size_t nbat = skyrc_operations_get_battery_type_count(ops);
    for (size_t bi = 0; bi < nbat; ++bi) {
        const char *bname = skyrc_operations_get_battery_type(ops, bi);
        printf("  [%zu] %s\n", bi, bname ? bname : "?");
        size_t nmode = skyrc_operations_get_mode_count(ops, bi);
        for (size_t mi = 0; mi < nmode; ++mi) {
            const char *mname = skyrc_operations_get_mode(ops, bi, mi);
            printf("       [%zu] %s\n", mi, mname ? mname : "?");
        }
    }
    return 0;
}

/* ============================================================ cmd_status */

static void print_current_state(const skyrc_current_state *st,
                                const skyrc_operations *ops,
                                const char *ch)
{
    printf("  Channel %s:\n", ch);
    if (!skyrc_current_state_is_working(st)) {
        printf("    Status          : idle (work_mode=%u)\n",
               skyrc_current_state_get_work_mode(st));
    } else {
        uint8_t bi    = skyrc_current_state_get_battery_index(st);
        uint8_t mi    = skyrc_current_state_get_mode_index(st);
        const char *bn = ops ? skyrc_operations_get_battery_type(ops, bi) : NULL;
        const char *mn = ops ? skyrc_operations_get_mode(ops, bi, mi)     : NULL;
        printf("    Status          : active\n");
        printf("    Work mode byte  : %u\n", skyrc_current_state_get_work_mode(st));
        printf("    Battery         : %s (index %u)\n", bn ? bn : "?", bi);
        printf("    Mode            : %s (index %u)\n", mn ? mn : "?", mi);
        printf("    Cell count      : %u\n", skyrc_current_state_get_cell_count(st));
    }
    printf("    Max charge curr : %u mA\n",
           skyrc_current_state_get_max_charge_current(st));
    printf("    Max discharge   : %u mA\n",
           skyrc_current_state_get_max_discharge_current(st));
}

static int cmd_status(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "status: <host> required\n"); return 1; }
    const char *host = argv[0];

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_device_type dtype = skyrc_device_get_type(dev);
    bool is_dx00 = (dtype == SKYRC_D100 || dtype == SKYRC_D200);
    const skyrc_operations *ops = skyrc_get_operations(dtype);
    printf("Host: %s  Model: %s\n", host, device_type_name(dtype));

    skyrc_current_state *st = skyrc_current_state_create();
    if (!st) {
        fprintf(stderr, "status: allocation failure\n");
        skyrc_free_item(dev);
        return 1;
    }

    int rc = 0;
    enum skyrc_error err = skyrc_request_current_state(dev, SKYRC_STATE_A);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "status: request failed (A): %s\n", skyrc_error_name(err));
        rc = 1;
    } else if (skyrc_device_get_current_state(dev, SKYRC_STATE_A, st)) {
        print_current_state(st, ops, "A");
    }

    if (is_dx00) {
        err = skyrc_request_current_state(dev, SKYRC_STATE_B);
        if (err != SKYRC_NO_ERROR) {
            fprintf(stderr, "status: request failed (B): %s\n", skyrc_error_name(err));
            rc = 1;
        } else if (skyrc_device_get_current_state(dev, SKYRC_STATE_B, st)) {
            print_current_state(st, ops, "B");
        }
    }

    skyrc_current_state_free(st);
    skyrc_free_item(dev);
    return rc;
}

/* ============================================================ cmd_info */

static void print_system_info(const skyrc_system_info *info, const char *ch)
{
    printf("  Channel %s system info:\n", ch);
    printf("    Key beep           : %s\n",
           skyrc_system_info_get_key_beep(info) ? "on" : "off");
    printf("    System beep        : %s\n",
           skyrc_system_info_get_system_beep(info) ? "on" : "off");

    printf("    Capacity protection: %s",
           skyrc_system_info_get_capacity_enabled(info) ? "on" : "off");
    if (skyrc_system_info_get_capacity_enabled(info))
        printf("  (%u mAh)", skyrc_system_info_get_capacity_mah(info));
    putchar('\n');

    printf("    Time protection    : %s",
           skyrc_system_info_get_time_protection_enabled(info) ? "on" : "off");
    if (skyrc_system_info_get_time_protection_enabled(info))
        printf("  (%u min)", skyrc_system_info_get_time_protection_minutes(info));
    putchar('\n');

    printf("    Temp protection    : %s",
           skyrc_system_info_get_temperature_protection_enabled(info) ? "on" : "off");
    if (skyrc_system_info_get_temperature_protection_enabled(info))
        printf("  (%u deg C)", skyrc_system_info_get_temperature(info));
    putchar('\n');

    printf("    Rest time          : %u min\n",
           skyrc_system_info_get_rest_time_minutes(info));
    printf("    DC setting         : %u\n", skyrc_system_info_get_dc_setting(info));
    printf("    AC setting         : %u\n", skyrc_system_info_get_ac_setting(info));
    printf("    NiMH peak detect   : %u\n", skyrc_system_info_get_ni_mh_peak(info));
    printf("    NiCd peak detect   : %u\n", skyrc_system_info_get_ni_cd_peak(info));

    bool any = false;
    for (size_t i = 0; i < 6; ++i) {
        uint16_t mv = 0;
        if (skyrc_system_info_get_cell_voltage(info, i, &mv) && mv > 0) {
            if (!any) { printf("    Cell voltages      :"); any = true; }
            printf("  C%zu=%u mV", i + 1, mv);
        }
    }
    if (any) putchar('\n');
}

static int cmd_info(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "info: <host> required\n"); return 1; }
    const char *host = argv[0];

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_device_type dtype = skyrc_device_get_type(dev);
    bool is_dx00 = (dtype == SKYRC_D100 || dtype == SKYRC_D200);
    printf("Host: %s  Model: %s\n", host, device_type_name(dtype));

    skyrc_system_info *info = skyrc_system_info_create();
    if (!info) {
        fprintf(stderr, "info: allocation failure\n");
        skyrc_free_item(dev);
        return 1;
    }

    int rc = 0;
    enum skyrc_error err;
    if (is_dx00) {
        err = skyrc_request_system_info_dx00_a(dev);
        if (err != SKYRC_NO_ERROR) {
            fprintf(stderr, "info: request failed (A): %s\n", skyrc_error_name(err));
            rc = 1;
        } else if (skyrc_device_get_system_info(dev, SKYRC_STATE_A, info)) {
            print_system_info(info, "A");
        }
        err = skyrc_request_system_info_dx00_b(dev);
        if (err != SKYRC_NO_ERROR) {
            fprintf(stderr, "info: request failed (B): %s\n", skyrc_error_name(err));
            rc = 1;
        } else if (skyrc_device_get_system_info(dev, SKYRC_STATE_B, info)) {
            print_system_info(info, "B");
        }
    } else {
        err = skyrc_request_system_info_standard(dev);
        if (err != SKYRC_NO_ERROR) {
            fprintf(stderr, "info: request failed: %s\n", skyrc_error_name(err));
            rc = 1;
        } else if (skyrc_device_get_system_info(dev, SKYRC_STATE_A, info)) {
            print_system_info(info, "A");
        }
    }

    skyrc_system_info_free(info);
    skyrc_free_item(dev);
    return rc;
}

/* ============================================================ cmd_monitor */

static volatile sig_atomic_t g_stop = 0;

static void handle_sigint(int sig) { (void)sig; g_stop = 1; }

static void print_real_data(const skyrc_real_data_a *data, const char *ch)
{
    printf("  [%s] mode=%-2u  %6.3f V  %5.3f A  %5u mAh  t=%u s"
           "  ext=%u C  int=%u C",
           ch,
           skyrc_real_data_a_get_mode(data),
           (double)skyrc_real_data_a_get_voltage(data),
           (double)skyrc_real_data_a_get_current(data),
           skyrc_real_data_a_get_capacity(data),
           skyrc_real_data_a_get_time(data),
           skyrc_real_data_a_get_ext_temp(data),
           skyrc_real_data_a_get_int_temp(data));

    bool any = false;
    for (size_t i = 0; i < 6; ++i) {
        uint16_t mv = 0;
        if (skyrc_real_data_a_get_cell(data, i, &mv) && mv > 0) {
            if (!any) { printf("  cells:"); any = true; }
            printf(" C%zu=%u", i + 1, mv);
        }
    }
    uint8_t ec0 = 0, ec1 = 0;
    if (skyrc_real_data_a_get_error_code(data, 0, &ec0) && ec0)
        printf("  err0=0x%02x", ec0);
    if (skyrc_real_data_a_get_error_code(data, 1, &ec1) && ec1)
        printf("  err1=0x%02x", ec1);
    putchar('\n');
}

static int cmd_monitor(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "monitor: <host> required\n"); return 1; }
    const char *host = argv[0];

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_device_type dtype = skyrc_device_get_type(dev);
    bool is_dx00 = (dtype == SKYRC_D100 || dtype == SKYRC_D200);
    printf("Host: %s  Model: %s  (Ctrl-C to stop)\n",
           host, device_type_name(dtype));
    signal(SIGINT, handle_sigint);

    skyrc_real_data_a *data = skyrc_real_data_a_create();
    if (!data) {
        fprintf(stderr, "monitor: allocation failure\n");
        skyrc_free_item(dev);
        return 1;
    }

    int rc = 0;
    while (!g_stop) {
        enum skyrc_error err = skyrc_get_real_data(dev, SKYRC_STATE_A, data);
        if (err != SKYRC_NO_ERROR) {
            fprintf(stderr, "\nmonitor: read failed (A): %s\n",
                    skyrc_error_name(err));
            rc = 1; break;
        }
        print_real_data(data, "A");
        if (is_dx00 && !g_stop) {
            err = skyrc_get_real_data(dev, SKYRC_STATE_B, data);
            if (err != SKYRC_NO_ERROR) {
                fprintf(stderr, "\nmonitor: read failed (B): %s\n",
                        skyrc_error_name(err));
                rc = 1; break;
            }
            print_real_data(data, "B");
        }
        if (!g_stop) sleep(1);
    }

    putchar('\n');
    skyrc_real_data_a_free(data);
    skyrc_free_item(dev);
    return rc;
}

/* ============================================================ cmd_start */

/** Resolve battery name or --battery-index to an index into ops. */
static bool resolve_battery(const char *name, const char *idx_str,
                            const skyrc_operations *ops, const char *model_str,
                            uint8_t *out)
{
    if (idx_str) {
        return parse_uint8(idx_str, out);
    }
    if (!name) {
        fprintf(stderr, "start: --battery <name> or --battery-index <N> required\n"
                "  Run 'list-ops %s' to see available types.\n", model_str);
        return false;
    }
    size_t nbat = skyrc_operations_get_battery_type_count(ops);
    for (size_t i = 0; i < nbat; ++i) {
        const char *n = skyrc_operations_get_battery_type(ops, i);
        if (n && strcasecmp(n, name) == 0) { *out = (uint8_t)i; return true; }
    }
    fprintf(stderr, "start: battery type '%s' not found for this model.\n"
            "  Run 'list-ops %s' to see available types.\n", name, model_str);
    return false;
}

/** Resolve mode name or --mode-index to an index within the battery type. */
static bool resolve_mode(const char *name, const char *idx_str,
                         const skyrc_operations *ops, uint8_t battery_index,
                         const char *model_str, uint8_t *out)
{
    if (idx_str) {
        return parse_uint8(idx_str, out);
    }
    if (!name) {
        fprintf(stderr, "start: --mode <name> or --mode-index <N> required\n"
                "  Run 'list-ops %s' to see available modes.\n", model_str);
        return false;
    }
    size_t nmode = skyrc_operations_get_mode_count(ops, battery_index);
    for (size_t i = 0; i < nmode; ++i) {
        const char *n = skyrc_operations_get_mode(ops, battery_index, i);
        if (n && strcasecmp(n, name) == 0) { *out = (uint8_t)i; return true; }
    }
    fprintf(stderr, "start: mode '%s' not found for that battery type.\n"
            "  Run 'list-ops %s' to see available modes.\n", name, model_str);
    return false;
}

static int cmd_start(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "start: <host> required\n"); return 1; }
    const char *host = argv[0];

    /* --model is mandatory: skyrc_open_device sets device type to UNKNOWN,
       so skyrc_start would fail without explicitly setting it. */
    const char *model_str = find_opt(argc, argv, "--model");
    if (!model_str) {
        fprintf(stderr,
                "start: --model <model> is required.\n"
                "  Known models: d100, d200, b6mini, b6ac, b6acadd, w1000\n"
                "  Run 'list-ops <model>' to see battery types and modes.\n");
        return 1;
    }
    enum skyrc_device_type model_type = parse_model(model_str);
    if (model_type == SKYRC_UNKNOWN) {
        fprintf(stderr, "start: unknown model '%s'\n"
                "  Known models: d100, d200, b6mini, b6ac, b6acadd, w1000\n",
                model_str);
        return 1;
    }
    const skyrc_operations *ops = skyrc_get_operations(model_type);

    /* --channel */
    enum skyrc_device_state channel = SKYRC_STATE_A;
    const char *ch_str = find_opt(argc, argv, "--channel");
    if (ch_str) {
        if      (strcasecmp(ch_str, "A") == 0) channel = SKYRC_STATE_A;
        else if (strcasecmp(ch_str, "B") == 0) channel = SKYRC_STATE_B;
        else {
            fprintf(stderr, "start: --channel must be A or B\n");
            return 1;
        }
    }

    /* battery */
    uint8_t battery_index = 0;
    if (!resolve_battery(find_opt(argc, argv, "--battery"),
                         find_opt(argc, argv, "--battery-index"),
                         ops, model_str, &battery_index))
        return 1;

    /* mode */
    uint8_t mode_index = 0;
    if (!resolve_mode(find_opt(argc, argv, "--mode"),
                      find_opt(argc, argv, "--mode-index"),
                      ops, battery_index, model_str, &mode_index))
        return 1;

    /* --cells (required) */
    const char *cells_str = find_opt(argc, argv, "--cells");
    if (!cells_str) {
        fprintf(stderr, "start: --cells <count> is required\n"); return 1;
    }
    uint8_t cell_count = 0;
    if (!parse_uint8(cells_str, &cell_count) || cell_count == 0) {
        fprintf(stderr, "start: invalid --cells value '%s'\n", cells_str); return 1;
    }

    /* --charge-current in amperes (required) */
    const char *cc_str = find_opt(argc, argv, "--charge-current");
    if (!cc_str) {
        fprintf(stderr, "start: --charge-current <A> is required (e.g., 1.0)\n");
        return 1;
    }
    uint16_t charge_current = 0;
    if (!amps_to_ma(cc_str, &charge_current)) {
        fprintf(stderr, "start: invalid --charge-current '%s'\n", cc_str); return 1;
    }

    /* Optional parameters — default 0 means "charger uses its own default". */
    uint16_t discharge_current = 0;
    const char *dc_str = find_opt(argc, argv, "--discharge-current");
    if (dc_str && !amps_to_ma(dc_str, &discharge_current)) {
        fprintf(stderr, "start: invalid --discharge-current '%s'\n", dc_str); return 1;
    }

    uint16_t cutoff_voltage = 0;
    const char *cv_str = find_opt(argc, argv, "--cutoff-voltage");
    if (cv_str && !volts_to_mv(cv_str, &cutoff_voltage)) {
        fprintf(stderr, "start: invalid --cutoff-voltage '%s'\n", cv_str); return 1;
    }

    uint16_t charge_voltage = 0;
    const char *chv_str = find_opt(argc, argv, "--charge-voltage");
    if (chv_str && !volts_to_mv(chv_str, &charge_voltage)) {
        fprintf(stderr, "start: invalid --charge-voltage '%s'\n", chv_str); return 1;
    }

    uint8_t charge_voltage_profile = 0;
    const char *cvp_str = find_opt(argc, argv, "--charge-voltage-profile");
    if (cvp_str && !parse_uint8(cvp_str, &charge_voltage_profile)) {
        fprintf(stderr, "start: invalid --charge-voltage-profile '%s'\n", cvp_str);
        return 1;
    }

    uint8_t repeat_peak = 0;
    const char *rp_str = find_opt(argc, argv, "--repeat-peak");
    if (rp_str && !parse_uint8(rp_str, &repeat_peak)) {
        fprintf(stderr, "start: invalid --repeat-peak '%s'\n", rp_str); return 1;
    }

    uint8_t cycle_mode = 0;
    const char *cm_str = find_opt(argc, argv, "--cycle-mode");
    if (cm_str && !parse_uint8(cm_str, &cycle_mode)) {
        fprintf(stderr, "start: invalid --cycle-mode '%s'\n", cm_str); return 1;
    }

    uint8_t cycle_count = 0;
    const char *ccnt_str = find_opt(argc, argv, "--cycle-count");
    if (ccnt_str && !parse_uint8(ccnt_str, &cycle_count)) {
        fprintf(stderr, "start: invalid --cycle-count '%s'\n", ccnt_str); return 1;
    }

    uint8_t rest_time = 0;
    const char *rt_str = find_opt(argc, argv, "--rest-time");
    if (rt_str && !parse_uint8(rt_str, &rest_time)) {
        fprintf(stderr, "start: invalid --rest-time '%s'\n", rt_str); return 1;
    }

    uint16_t trickle_current = 0;
    const char *tc_str = find_opt(argc, argv, "--trickle-current");
    if (tc_str && !amps_to_ma(tc_str, &trickle_current)) {
        fprintf(stderr, "start: invalid --trickle-current '%s'\n", tc_str); return 1;
    }

    /* Connect and send program. */
    skyrc_device *dev = NULL;
    if (!skyrc_open_device(host, &dev)) {
        fprintf(stderr, "start: could not connect to %s\n", host);
        return 1;
    }
    skyrc_device_set_type(dev, model_type);

    const char *bname = skyrc_operations_get_battery_type(ops, battery_index);
    const char *mname = skyrc_operations_get_mode(ops, battery_index, mode_index);
    printf("Starting: host=%s  model=%s  battery=%s  mode=%s  "
           "channel=%s  cells=%u  charge=%.3f A\n",
           host, device_type_name(model_type),
           bname ? bname : "?", mname ? mname : "?",
           channel == SKYRC_STATE_A ? "A" : "B",
           cell_count, charge_current / 1000.0);

    enum skyrc_error err = skyrc_start(dev, channel,
                                       battery_index, cell_count, mode_index,
                                       charge_current, discharge_current,
                                       cutoff_voltage, charge_voltage,
                                       charge_voltage_profile, repeat_peak,
                                       cycle_mode, cycle_count,
                                       rest_time, trickle_current);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "start: %s\n", skyrc_error_name(err));
        return 1;
    }
    printf("Program started.\n");
    return 0;
}

/* ============================================================ cmd_stop */

static int cmd_stop(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "stop: <host> required\n"); return 1; }
    const char *host = argv[0];

    int channel = 1;
    const char *ch_str = find_opt(argc, argv, "--channel");
    if (ch_str) {
        char *end;
        long v = strtol(ch_str, &end, 10);
        if (end == ch_str || *end || v < 1 || v > 2) {
            fprintf(stderr, "stop: --channel must be 1 or 2\n"); return 1;
        }
        channel = (int)v;
    }

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    printf("Stopping channel %d on %s...\n", channel, host);
    enum skyrc_error err = skyrc_stop(dev, channel);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "stop: %s\n", skyrc_error_name(err));
        return 1;
    }
    printf("Stopped.\n");
    return 0;
}

/* ========================================================= cmd_set_speaker */

static bool parse_onoff(const char *s, bool *out, const char *flag)
{
    if (strcasecmp(s, "on")  == 0) { *out = true;  return true; }
    if (strcasecmp(s, "off") == 0) { *out = false; return true; }
    fprintf(stderr, "set-speaker: %s must be on or off\n", flag);
    return false;
}

static int cmd_set_speaker(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "set-speaker: <host> required\n"); return 1; }
    const char *host = argv[0];

    const char *key_str    = find_opt(argc, argv, "--key");
    const char *buzzer_str = find_opt(argc, argv, "--buzzer");
    if (!key_str || !buzzer_str) {
        fprintf(stderr,
                "set-speaker: --key on|off and --buzzer on|off required\n");
        return 1;
    }

    bool key = false, buzzer = false;
    if (!parse_onoff(key_str,    &key,    "--key"))    return 1;
    if (!parse_onoff(buzzer_str, &buzzer, "--buzzer")) return 1;

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_error err = skyrc_ext_set_speaker(dev, key, buzzer);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "set-speaker: %s\n", skyrc_error_name(err)); return 1;
    }
    printf("Beep settings updated: key=%s  buzzer=%s\n",
           key ? "on" : "off", buzzer ? "on" : "off");
    return 0;
}

/* ======================================================== cmd_set_capacity */

static int cmd_set_capacity(int argc, char **argv)
{
    /* argv[0]=host  argv[1]=on|off  [--limit mAh] */
    if (argc < 2) {
        fprintf(stderr, "set-capacity: <host> on|off [--limit mAh]\n");
        return 1;
    }
    const char *host = argv[0];
    const char *onoff = argv[1];
    bool on;
    if      (strcasecmp(onoff, "on")  == 0) on = true;
    else if (strcasecmp(onoff, "off") == 0) on = false;
    else {
        fprintf(stderr, "set-capacity: second argument must be on or off\n");
        return 1;
    }

    uint16_t limit = 0;
    const char *lim_str = find_opt(argc, argv, "--limit");
    if (lim_str && !parse_uint16(lim_str, &limit)) {
        fprintf(stderr, "set-capacity: invalid --limit value\n"); return 1;
    }
    if (on && limit == 0) {
        fprintf(stderr, "set-capacity: --limit <mAh> required when enabling\n");
        return 1;
    }

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_error err = skyrc_ext_set_capacity(dev, on, limit);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "set-capacity: %s\n", skyrc_error_name(err)); return 1;
    }
    if (on)
        printf("Capacity protection enabled at %u mAh.\n", limit);
    else
        printf("Capacity protection disabled.\n");
    return 0;
}

/* ================================================= cmd_set_time_protection */

static int cmd_set_time_protection(int argc, char **argv)
{
    /* argv[0]=host  argv[1]=on|off  [--minutes N] */
    if (argc < 2) {
        fprintf(stderr, "set-time-protection: <host> on|off [--minutes N]\n");
        return 1;
    }
    const char *host = argv[0];
    const char *onoff = argv[1];
    bool on;
    if      (strcasecmp(onoff, "on")  == 0) on = true;
    else if (strcasecmp(onoff, "off") == 0) on = false;
    else {
        fprintf(stderr,
                "set-time-protection: second argument must be on or off\n");
        return 1;
    }

    uint16_t minutes = 0;
    const char *min_str = find_opt(argc, argv, "--minutes");
    if (min_str && !parse_uint16(min_str, &minutes)) {
        fprintf(stderr, "set-time-protection: invalid --minutes value\n");
        return 1;
    }
    if (on && minutes == 0) {
        fprintf(stderr,
                "set-time-protection: --minutes <N> required when enabling\n");
        return 1;
    }

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_error err = skyrc_ext_set_time(dev, on, minutes);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "set-time-protection: %s\n", skyrc_error_name(err));
        return 1;
    }
    if (on)
        printf("Time protection enabled at %u min.\n", minutes);
    else
        printf("Time protection disabled.\n");
    return 0;
}

/* ======================================================= cmd_set_rest_time */

static int cmd_set_rest_time(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "set-rest-time: <host> required\n"); return 1; }
    const char *host = argv[0];

    const char *min_str = find_opt(argc, argv, "--minutes");
    if (!min_str) {
        fprintf(stderr, "set-rest-time: --minutes <N> required\n"); return 1;
    }
    uint8_t minutes = 0;
    if (!parse_uint8(min_str, &minutes)) {
        fprintf(stderr, "set-rest-time: invalid --minutes value\n"); return 1;
    }

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_error err = skyrc_ext_set_rest_time(dev, minutes);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "set-rest-time: %s\n", skyrc_error_name(err)); return 1;
    }
    printf("Rest time set to %u min.\n", minutes);
    return 0;
}

/* =========================================================== cmd_set_temp */

static int cmd_set_temp(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "set-temp: <host> required\n"); return 1; }
    const char *host = argv[0];

    const char *thr_str = find_opt(argc, argv, "--threshold");
    if (!thr_str) {
        fprintf(stderr, "set-temp: --threshold <N> required\n"); return 1;
    }
    uint8_t threshold = 0;
    if (!parse_uint8(thr_str, &threshold)) {
        fprintf(stderr, "set-temp: invalid --threshold value\n"); return 1;
    }

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    enum skyrc_error err = skyrc_ext_set_temp(dev, threshold);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "set-temp: %s\n", skyrc_error_name(err)); return 1;
    }
    printf("Temperature threshold set to %u.\n", threshold);
    return 0;
}

/* =========================================================== cmd_wifi_scan */

static int cmd_wifi_scan(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "wifi-scan: <host> required\n"); return 1; }
    const char *host = argv[0];

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    /* Ask for up to 32 kB; resize and retry once if it doesn't fit. */
    size_t cap = 8192;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "wifi-scan: allocation failure\n");
        skyrc_free_item(dev);
        return 1;
    }

    size_t needed = 0;
    enum skyrc_error err = skyrc_wifi_scan(dev, buf, cap, &needed);
    if (err == SKYRC_INVALID_PACKET_SIZE && needed > cap) {
        free(buf);
        cap = needed;
        buf = malloc(cap);
        if (!buf) {
            fprintf(stderr, "wifi-scan: allocation failure\n");
            skyrc_free_item(dev);
            return 1;
        }
        err = skyrc_wifi_scan(dev, buf, cap, &needed);
    }
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        free(buf);
        fprintf(stderr, "wifi-scan: %s\n", skyrc_error_name(err));
        return 1;
    }
    printf("%s\n", buf);
    free(buf);
    return 0;
}

/* ======================================================= cmd_wifi_connect */

static int cmd_wifi_connect(int argc, char **argv)
{
    if (argc < 1) { fprintf(stderr, "wifi-connect: <host> required\n"); return 1; }
    const char *host = argv[0];

    const char *ssid     = find_opt(argc, argv, "--ssid");
    const char *password = find_opt(argc, argv, "--password");
    const char *sec_str  = find_opt(argc, argv, "--security");

    if (!ssid || !password || !sec_str) {
        fprintf(stderr,
                "wifi-connect: --ssid <name> --password <pass> --security <N> required\n"
                "  Use 'wifi-scan <host>' first to discover networks and security values.\n"
                "  Use an empty string (\"\") for --password on open networks.\n");
        return 1;
    }
    uint8_t security = 0;
    if (!parse_uint8(sec_str, &security)) {
        fprintf(stderr, "wifi-connect: invalid --security value\n"); return 1;
    }

    skyrc_device *dev = open_device_with_model(host, argc, argv);
    if (!dev) return 1;

    printf("Connecting charger at %s to Wi-Fi network '%s'...\n", host, ssid);
    enum skyrc_error err = skyrc_wifi_configure(dev, ssid, password, security);
    skyrc_free_item(dev);
    if (err != SKYRC_NO_ERROR) {
        fprintf(stderr, "wifi-connect: %s\n", skyrc_error_name(err)); return 1;
    }
    printf("Wi-Fi configured. The charger will reconnect on the new network.\n");
    return 0;
}

/* ================================================================= usage */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <subcommand> [arguments]\n"
        "\n"
        "Discovery:\n"
        "  discover\n"
        "      Broadcast on UDP port 8888 and list responding chargers.\n"
        "\n"
        "  list-ops <model>\n"
        "      List battery types and modes for model without connecting.\n"
        "      Models: d100, d200, b6mini, b6ac, b6acadd, w1000\n"
        "\n"
        "Read state  (--model sets type when connecting by IP):\n"
        "  status  <host> [--model M]\n"
        "  info    <host> [--model M]\n"
        "  monitor <host> [--model M]        (Ctrl-C to stop)\n"
        "\n"
        "Charge control:\n"
        "  start <host> --model M --battery B --mode M --cells N\n"
        "               --charge-current A\n"
        "               [--channel A|B] [--discharge-current A]\n"
        "               [--cutoff-voltage V] [--charge-voltage V]\n"
        "               [--charge-voltage-profile N] [--repeat-peak N]\n"
        "               [--cycle-mode N] [--cycle-count N]\n"
        "               [--rest-time min] [--trickle-current A]\n"
        "      Currents in amperes (e.g., 1.5).  Voltages in volts (e.g., 4.20).\n"
        "      Use 'list-ops <model>' to see valid battery types and modes.\n"
        "\n"
        "  stop  <host> [--model M] [--channel 1|2]\n"
        "\n"
        "Settings:\n"
        "  set-speaker         <host> [--model M] --key on|off --buzzer on|off\n"
        "  set-capacity        <host> [--model M] on|off [--limit mAh]\n"
        "  set-time-protection <host> [--model M] on|off [--minutes N]\n"
        "  set-rest-time       <host> [--model M] --minutes N\n"
        "  set-temp            <host> [--model M] --threshold N\n"
        "\n"
        "Wi-Fi:\n"
        "  wifi-scan    <host> [--model M]\n"
        "  wifi-connect <host> [--model M] --ssid S --password P --security N\n",
        prog);
}

/* ================================================================= main */

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *cmd  = argv[1];
    int   sub_argc   = argc - 2;
    char **sub_argv  = argv + 2;

    if (strcmp(cmd, "discover") == 0)
        return cmd_discover();

    if (strcmp(cmd, "list-ops") == 0) {
        if (sub_argc < 1) {
            fprintf(stderr, "list-ops: <model> required\n"); return 1;
        }
        return cmd_list_ops(sub_argv[0]);
    }

    if (strcmp(cmd, "status")  == 0) return cmd_status(sub_argc, sub_argv);
    if (strcmp(cmd, "info")    == 0) return cmd_info(sub_argc, sub_argv);
    if (strcmp(cmd, "monitor") == 0) return cmd_monitor(sub_argc, sub_argv);
    if (strcmp(cmd, "start")   == 0) return cmd_start(sub_argc, sub_argv);
    if (strcmp(cmd, "stop")    == 0) return cmd_stop(sub_argc, sub_argv);

    if (strcmp(cmd, "set-speaker")         == 0) return cmd_set_speaker(sub_argc, sub_argv);
    if (strcmp(cmd, "set-capacity")        == 0) return cmd_set_capacity(sub_argc, sub_argv);
    if (strcmp(cmd, "set-time-protection") == 0) return cmd_set_time_protection(sub_argc, sub_argv);
    if (strcmp(cmd, "set-rest-time")       == 0) return cmd_set_rest_time(sub_argc, sub_argv);
    if (strcmp(cmd, "set-temp")            == 0) return cmd_set_temp(sub_argc, sub_argv);

    if (strcmp(cmd, "wifi-scan")    == 0) return cmd_wifi_scan(sub_argc, sub_argv);
    if (strcmp(cmd, "wifi-connect") == 0) return cmd_wifi_connect(sub_argc, sub_argv);

    fprintf(stderr, "skyrc: unknown subcommand '%s'\n\n", cmd);
    usage(argv[0]);
    return 1;
}
