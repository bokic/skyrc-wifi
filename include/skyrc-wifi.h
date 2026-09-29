#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef libskyrc_wifi_EXPORTS
 #if defined(_MSC_VER)
  #define EXPORT_SKYRC __declspec(dllexport)
 #else
  #define EXPORT_SKYRC __attribute__((visibility("default")))
 #endif
#else
 #define EXPORT_SKYRC
#endif

/** Default listener timeout in milliseconds. */
#define SKYRC_DEFAULT_TIMEOUT 500

/** Known charger families. */
enum skyrc_device_type {
    SKYRC_UNKNOWN,
    SKYRC_D100,
    SKYRC_D200,
    SKYRC_B6MINI,
    SKYRC_B6AC,
    SKYRC_B6ACADD,
    SKYRC_W1000,
};

/** Selects the charger channel used by channel-aware operations. */
enum skyrc_device_state {
    SKYRC_STATE_A,
    SKYRC_STATE_B,
};

/** Positive status codes returned by integer APIs; zero means success. */
enum skyrc_error {
    SKYRC_NO_ERROR,
    SKYRC_NULL_DEVICE_HANDLE,
    SKYRC_NULL_RESULT_HANDLE,
    SKYRC_FAIL_TO_CREATE_SOCKET,
    SKYRC_FAIL_TO_BIND_TO_SOCKET,
    SKYRC_SETSOCKOPT_FAILED,
    SKYRC_FAILED_TO_CLOSE_SOCKET,
    SKYRC_INVALID_SOCKET,
    SKYRC_INVALID_INPUT_PARAMETER,
    SKYRC_SENDTO_FAILED,
    SKYRC_UNABLE_TO_SEND_MESSAGE,
    SKYRC_UNABLE_TO_RECIEVE_MESSAGE,
    SKYRC_NO_PACKET_RECIEVED,
    SKYRC_EMPTY_PACKET_RECIEVED,
    SKYRC_INVALID_PACKET_SIZE,
    SKYRC_INVALID_STATUS,
    SKYRC_WRONG_REPLY_COMMAND_ID,
    SKYRC_UNKNOWN_DEVICE,
    SKYRC_UNSUPPORTED_COMMAND,
    SKYRC_UNKNOWN_ERROR,
};

typedef struct skyrc_device skyrc_device;
typedef struct skyrc_real_data_a skyrc_real_data_a;
typedef struct skyrc_operations skyrc_operations;
typedef struct skyrc_current_state skyrc_current_state;
typedef struct skyrc_system_info skyrc_system_info;

#ifdef __cplusplus
extern "C" {
#endif

/** Open an IPv4 UDP socket on the charger discovery port and set a receive timeout.
 * @param timeout Receive timeout in milliseconds.
 * @return Socket descriptor on success, or a positive skyrc_error value.
 */
EXPORT_SKYRC int skyrc_start_listen(int timeout);

/** Close a discovery socket.
 * @param socket Socket descriptor returned by skyrc_start_listen().
 * @return 0 on success, or SKYRC_FAILED_TO_CLOSE_SOCKET.
 */
EXPORT_SKYRC enum skyrc_error skyrc_stop_listen(int socket);

/** Send the LAN discovery broadcast.
 * @param sockfd Listening UDP socket descriptor.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_send_broadcast(int sockfd);

/** Receive and parse one charger discovery response.
 * @param sockfd Socket with a pending discovery response.
 * @return Newly allocated device on success; NULL for timeout, invalid reply, or allocation failure.
 */
EXPORT_SKYRC skyrc_device *skyrc_find_device(int sockfd);

/** Release a device and any socket and strings owned by it.
 * @param item Device returned by a discovery or connection function; NULL is allowed.
 */
EXPORT_SKYRC void skyrc_free_item(skyrc_device *item);

/** Resolve a hostname or numeric IPv4/IPv6 address and create a device handle and UDP socket on port 8888.
 * @param host Hostname or address literal (IPv6 zone identifiers are accepted).
 * @param device Receives the allocated device handle on success.
 * @return true on success, false for invalid input, resolution, or socket errors.
 */
EXPORT_SKYRC bool skyrc_open_device(const char *host, skyrc_device **device);

/** Check that a device handle has an initialized UDP socket and endpoint.
 * @param device Device handle to check.
 * @return true when the handle contains an initialized socket and endpoint.
 */
EXPORT_SKYRC bool skyrc_device_is_initialized(const skyrc_device *device);

/** Get the model family associated with a device.
 * @param device Device handle.
 * @return Model family, or SKYRC_UNKNOWN for NULL.
 */
EXPORT_SKYRC enum skyrc_device_type skyrc_device_get_type(const skyrc_device *device);

/** Override the model family stored in a device handle.
 *  Use this after skyrc_open_device() when the charger model is known but was
 *  not discovered automatically (discovery sets the type; direct connections do not).
 * @param device Device handle; NULL is allowed (no-op).
 * @param type   Model family to store.
 */
EXPORT_SKYRC void skyrc_device_set_type(skyrc_device *device, enum skyrc_device_type type);

/** Get the device's numeric address string.
 * @param device Device handle.
 * @return Borrowed address string, or NULL for NULL device.
 */
EXPORT_SKYRC const char *skyrc_device_get_ip(const skyrc_device *device);

/** Get the device identifier from discovery data.
 * @param device Device handle.
 * @return Borrowed identifier string, or NULL if unavailable.
 */
EXPORT_SKYRC const char *skyrc_device_get_id(const skyrc_device *device);

/** Get the advertised device name.
 * @param device Device handle.
 * @return Borrowed name string, or NULL if unavailable.
 */
EXPORT_SKYRC const char *skyrc_device_get_name(const skyrc_device *device);

/** Get the advertised device MAC address.
 * @param device Device handle.
 * @return Borrowed MAC string, or NULL if unavailable.
 */
EXPORT_SKYRC const char *skyrc_device_get_mac(const skyrc_device *device);

/** Get the advertised firmware version.
 * @param device Device handle.
 * @return Borrowed version string, or NULL if unavailable.
 */
EXPORT_SKYRC const char *skyrc_device_get_version(const skyrc_device *device);

/** Get the advertised network password.
 * @param device Device handle.
 * @return Borrowed password string, or NULL if unavailable.
 */
EXPORT_SKYRC const char *skyrc_device_get_password(const skyrc_device *device);

/** Get the advertised network mode.
 * @param device Device handle.
 * @return Borrowed mode string, or NULL if unavailable.
 */
EXPORT_SKYRC const char *skyrc_device_get_mode(const skyrc_device *device);

/** Get static battery and mode metadata for a charger family.
 * @param device_type Value from skyrc_device_type.
 * @return Borrowed operations table, or NULL for unsupported/unknown family.
 */
EXPORT_SKYRC const skyrc_operations *skyrc_get_operations(enum skyrc_device_type device_type);

/** Count supported battery types.
 * @param operations Operations table from skyrc_get_operations().
 * @return Number of battery types, or zero for NULL.
 */
EXPORT_SKYRC size_t skyrc_operations_get_battery_type_count(const skyrc_operations *operations);

/** Get a battery type name.
 * @param operations Operations table.
 * @param battery_index Zero-based battery type index.
 * @return Borrowed static name, or NULL for an invalid index/table.
 */
EXPORT_SKYRC const char *skyrc_operations_get_battery_type(const skyrc_operations *operations, size_t battery_index);

/** Count modes available for one battery type.
 * @param operations Operations table.
 * @param battery_index Zero-based battery type index.
 * @return Number of modes, or zero for an invalid index/table.
 */
EXPORT_SKYRC size_t skyrc_operations_get_mode_count(const skyrc_operations *operations, size_t battery_index);

/** Get a mode name for a battery type.
 * @param operations Operations table.
 * @param battery_index Zero-based battery type index.
 * @param mode_index Zero-based mode index.
 * @return Borrowed static name, or NULL for an invalid index/table.
 */
EXPORT_SKYRC const char *skyrc_operations_get_mode(const skyrc_operations *operations, size_t battery_index, size_t mode_index);

/** Allocate storage for live charger measurements.
 * @return New result object, or NULL on allocation failure.
 */
EXPORT_SKYRC skyrc_real_data_a *skyrc_real_data_a_create(void);

/** Free a live-measurement result object.
 * @param data Result object; NULL is allowed.
 */
EXPORT_SKYRC void skyrc_real_data_a_free(skyrc_real_data_a *data);

/** Get the reported work mode.
 * @param data Parsed live data.
 * @return Mode byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_real_data_a_get_mode(const skyrc_real_data_a *data);

/** Get measured capacity in milliamp-hours.
 * @param data Parsed live data.
 * @return Capacity, or zero for NULL.
 */
EXPORT_SKYRC uint16_t skyrc_real_data_a_get_capacity(const skyrc_real_data_a *data);

/** Get the elapsed-time value reported by the charger.
 * @param data Parsed live data.
 * @return Raw elapsed-time value, or zero for NULL.
 */
EXPORT_SKYRC uint16_t skyrc_real_data_a_get_time(const skyrc_real_data_a *data);

/** Get measured voltage in volts.
 * @param data Parsed live data.
 * @return Voltage, or 0.0 for NULL.
 */
EXPORT_SKYRC float skyrc_real_data_a_get_voltage(const skyrc_real_data_a *data);

/** Get measured current in amperes.
 * @param data Parsed live data.
 * @return Current, or 0.0 for NULL.
 */
EXPORT_SKYRC float skyrc_real_data_a_get_current(const skyrc_real_data_a *data);

/** Get external temperature in degrees Celsius.
 * @param data Parsed live data.
 * @return Temperature byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_real_data_a_get_ext_temp(const skyrc_real_data_a *data);

/** Get internal temperature in degrees Celsius.
 * @param data Parsed live data.
 * @return Temperature byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_real_data_a_get_int_temp(const skyrc_real_data_a *data);

/** Get one cell voltage.
 * @param data Parsed live data.
 * @param cell_index Zero-based cell index (0–5).
 * @param value Receives the voltage in millivolts.
 * @return true on success, false for NULL arguments or invalid index.
 */
EXPORT_SKYRC bool skyrc_real_data_a_get_cell(const skyrc_real_data_a *data, size_t cell_index, uint16_t *value);

/** Get one reported error code.
 * @param data Parsed live data.
 * @param code_index Zero-based error-code index (0–1).
 * @param value Receives the code byte.
 * @return true on success, false for NULL arguments or invalid index.
 */
EXPORT_SKYRC bool skyrc_real_data_a_get_error_code(const skyrc_real_data_a *data, size_t code_index, uint8_t *value);

/** Request and cache current charger state for a channel.
 * @param device Connected charger.
 * @param state Channel A or B; non-D100/D200 models only support A.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_request_current_state(skyrc_device *device, enum skyrc_device_state state);

/** Copy the cached state for a channel into a caller-owned opaque result.
 * @param device Charger containing a previously received state.
 * @param state Channel whose cached result is requested.
 * @param result Caller-created result object to receive the state.
 * @return true when cached state is available and copied; false otherwise.
 */
EXPORT_SKYRC bool skyrc_device_get_current_state(const skyrc_device *device, enum skyrc_device_state state, skyrc_current_state *result);

/** Allocate an opaque current-state result object.
 * @return New object, or NULL on allocation failure.
 */
EXPORT_SKYRC skyrc_current_state *skyrc_current_state_create(void);

/** Free a current-state result object.
 * @param state Object to free; NULL is allowed.
 */
EXPORT_SKYRC void skyrc_current_state_free(skyrc_current_state *state);

/** Get the device's reported work-mode byte.
 * @param state Current-state result.
 * @return Work-mode byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_current_state_get_work_mode(const skyrc_current_state *state);

/** Check whether the charger reports an active program.
 * @param state Current-state result.
 * @return true when active; false for inactive or NULL.
 */
EXPORT_SKYRC bool skyrc_current_state_is_working(const skyrc_current_state *state);

/** Get the zero-based selected battery type.
 * @param state Current-state result.
 * @return Battery index, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_current_state_get_battery_index(const skyrc_current_state *state);

/** Get the configured battery cell count.
 * @param state Current-state result.
 * @return Cell count, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_current_state_get_cell_count(const skyrc_current_state *state);

/** Get the zero-based selected charge/discharge mode.
 * @param state Current-state result.
 * @return Mode index, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_current_state_get_mode_index(const skyrc_current_state *state);

/** Get the maximum supported charge current in milliamps.
 * @param state Current-state result.
 * @return Current limit, or zero for NULL.
 */
EXPORT_SKYRC uint16_t skyrc_current_state_get_max_charge_current(const skyrc_current_state *state);

/** Get the maximum supported discharge current in milliamps.
 * @param state Current-state result.
 * @return Current limit, or zero for NULL.
 */
EXPORT_SKYRC uint16_t skyrc_current_state_get_max_discharge_current(const skyrc_current_state *state);

/** Request live measurements and parse them into a caller-owned result.
 * @param device Connected charger.
 * @param state Channel A or B; non-D100/D200 models only support A.
 * @param result Caller-created result object to receive measurements.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_get_real_data(skyrc_device *device, enum skyrc_device_state state, skyrc_real_data_a *result);

/** Request and cache system configuration for standard charger models.
 * @param device Connected non-D100/D200 charger.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_request_system_info_standard(skyrc_device *device);

/** Request and cache D100/D200 channel A system configuration.
 * @param device Connected D100/D200 charger.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_request_system_info_dx00_a(skyrc_device *device);

/** Request and cache D100/D200 channel B system configuration.
 * @param device Connected D100/D200 charger.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_request_system_info_dx00_b(skyrc_device *device);

/** Copy cached system configuration into a caller-owned result object.
 * @param device Charger containing previously received system information.
 * @param state Channel A or B.
 * @param result Caller-created result object to receive the configuration.
 * @return true when cached data is available and copied; false otherwise.
 */
EXPORT_SKYRC bool skyrc_device_get_system_info(const skyrc_device *device, enum skyrc_device_state state, skyrc_system_info *result);

/** Allocate an opaque system-information result object.
 * @return New object, or NULL on allocation failure.
 */
EXPORT_SKYRC skyrc_system_info *skyrc_system_info_create(void);

/** Free a system-information result object.
 * @param info Object to free; NULL is allowed.
 */
EXPORT_SKYRC void skyrc_system_info_free(skyrc_system_info *info);

/** Get the key-beep setting.
 * @param info System-information result.
 * @return true when enabled; false when disabled or NULL.
 */
EXPORT_SKYRC bool skyrc_system_info_get_key_beep(const skyrc_system_info *info);

/** Get the system-beep setting.
 * @param info System-information result.
 * @return true when enabled; false when disabled or NULL.
 */
EXPORT_SKYRC bool skyrc_system_info_get_system_beep(const skyrc_system_info *info);

/** Check whether capacity protection is enabled.
 * @param info System-information result.
 * @return true when enabled; false when disabled or NULL.
 */
EXPORT_SKYRC bool skyrc_system_info_get_capacity_enabled(const skyrc_system_info *info);

/** Get configured capacity protection in milliamp-hours.
 * @param info System-information result.
 * @return Capacity, or zero for NULL.
 */
EXPORT_SKYRC uint16_t skyrc_system_info_get_capacity_mah(const skyrc_system_info *info);

/** Check whether time protection is enabled.
 * @param info System-information result.
 * @return true when enabled; false when disabled or NULL.
 */
EXPORT_SKYRC bool skyrc_system_info_get_time_protection_enabled(const skyrc_system_info *info);

/** Get the configured time-protection limit in minutes.
 * @param info System-information result.
 * @return Limit, or zero for NULL.
 */
EXPORT_SKYRC uint16_t skyrc_system_info_get_time_protection_minutes(const skyrc_system_info *info);

/** Get the configured rest interval in minutes.
 * @param info System-information result.
 * @return Rest interval, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_system_info_get_rest_time_minutes(const skyrc_system_info *info);

/** Get the configured temperature-protection threshold.
 * @param info System-information result.
 * @return Temperature byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_system_info_get_temperature(const skyrc_system_info *info);

/** Check whether temperature protection is enabled.
 * @param info System-information result.
 * @return true when enabled; false when disabled or NULL.
 */
EXPORT_SKYRC bool skyrc_system_info_get_temperature_protection_enabled(const skyrc_system_info *info);

/** Get the configured DC input setting.
 * @param info System-information result.
 * @return Setting byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_system_info_get_dc_setting(const skyrc_system_info *info);

/** Get the configured AC input setting.
 * @param info System-information result.
 * @return Setting byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_system_info_get_ac_setting(const skyrc_system_info *info);

/** Get the NiMH peak-detection setting.
 * @param info System-information result.
 * @return Setting byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_system_info_get_ni_mh_peak(const skyrc_system_info *info);

/** Get the NiCd peak-detection setting.
 * @param info System-information result.
 * @return Setting byte, or zero for NULL.
 */
EXPORT_SKYRC uint8_t skyrc_system_info_get_ni_cd_peak(const skyrc_system_info *info);

/** Get the cached voltage for one cell.
 * @param info System-information result.
 * @param cell_index Zero-based cell index (0–5).
 * @param millivolts Receives the voltage in millivolts.
 * @return true on success, false for NULL arguments or invalid index.
 */
EXPORT_SKYRC bool skyrc_system_info_get_cell_voltage(const skyrc_system_info *info, size_t cell_index, uint16_t *millivolts);

/** Send a charge/discharge program to the charger and start it.
 * @param device Connected charger with a known model family.
 * @param channel Channel A or B; standard models require A.
 * @param battery_index Zero-based battery type index from the model's operations table.
 * @param cell_count Battery cell count.
 * @param mode_index Zero-based mode index for battery_index.
 * @param charge_current Charge current in the APK's protocol units.
 * @param discharge_current Discharge current in the APK's protocol units.
 * @param cutoff_voltage_per_cell Cutoff voltage in protocol units; multiplied by cell_count for D100/D200.
 * @param charge_voltage Charge voltage in protocol units.
 * @param charge_voltage_profile D100/D200 profile byte; ignored for other families.
 * @param repeat_peak Re-peak count/setting.
 * @param cycle_mode D100/D200 or NiMH/NiCd cycle direction/mode.
 * @param cycle_count Number of cycles.
 * @param rest_time_minutes D100/D200 rest interval between cycle phases.
 * @param trickle_current D100/D200 trickle current in protocol units.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_start(
    skyrc_device *device,
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
    uint16_t trickle_current);

/** Stop the running program.
 * @param device Connected charger.
 * @param channel D100/D200 channel number (1 or 2); ignored on other models.
 * @return SKYRC_NO_ERROR on a valid stop acknowledgement, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_stop(skyrc_device *device, int channel);

/** Configure key and system beep settings.
 * @param device Connected charger.
 * @param key true to enable key beeps.
 * @param buzzer true to enable system beeps.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_ext_set_speaker(skyrc_device *device, bool key, bool buzzer);

/** Configure capacity protection.
 * @param device Connected charger.
 * @param on true to enable capacity protection.
 * @param capacity Capacity limit in milliamp-hours.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_ext_set_capacity(skyrc_device *device, bool on, uint16_t capacity);

/** Set the charger rest interval.
 * @param device Connected charger.
 * @param minutes Rest interval in minutes.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_ext_set_rest_time(skyrc_device *device, uint8_t minutes);

/** Configure time protection.
 * @param device Connected charger.
 * @param on true to enable time protection.
 * @param minutes Time limit in minutes.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_ext_set_time(skyrc_device *device, bool on, uint16_t minutes);

/** Set the charger temperature-protection threshold.
 * @param device Connected charger.
 * @param temperature Threshold byte in the protocol's temperature units.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_ext_set_temp(skyrc_device *device, uint8_t temperature);

/** Scan for Wi-Fi networks and copy the charger's returned JSON array.
 * @param device Connected charger.
 * @param networks_json Buffer receiving a NUL-terminated JSON array.
 * @param json_capacity Capacity of networks_json in bytes.
 * @param json_size Receives required buffer size, including the terminating NUL.
 * @return SKYRC_NO_ERROR on success, otherwise a skyrc_error value; an undersized buffer returns SKYRC_INVALID_PACKET_SIZE.
 */
EXPORT_SKYRC enum skyrc_error skyrc_wifi_scan(skyrc_device *device, char *networks_json,
                                 size_t json_capacity, size_t *json_size);

/** Configure the charger to join a Wi-Fi network as a client.
 * @param device Connected charger.
 * @param ssid Network name.
 * @param password Network password; use an empty string for an open network.
 * @param security Security value reported by the charger's Wi-Fi scan response.
 * @return SKYRC_NO_ERROR when the charger reports success, otherwise a skyrc_error value.
 */
EXPORT_SKYRC enum skyrc_error skyrc_wifi_configure(skyrc_device *device, const char *ssid,
                                      const char *password, uint8_t security);

#ifdef __cplusplus
}
#endif
