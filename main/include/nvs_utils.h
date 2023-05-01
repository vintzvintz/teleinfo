
#include "nvs.h"
#include "tic_types.h"

nvs_type_t str_to_nvstype(const char *type);
const char *nvstype_to_str(nvs_type_t type);

// alloue un buffer qui doit être libéré par l'appelant
tic_error_t console_nvs_get_string( const char* key, char **out_buf );

// alloue un buffer qui doit être libéré par l'appelant
tic_error_t console_nvs_get_blob( const char* key, char **out_buf, size_t *out_len );

// alloue un buffer qui doit être libéré par l'appelant 
tic_error_t console_nvs_get_blob_as_string( const char* key, char **out_buf );

//set/print value in any namespace
esp_err_t set_value_in_nvs(const char *namespace, const char *key, const char *str_type, const char *str_value);
esp_err_t print_value_from_nvs(const char *namespace, const char *key, const char *str_type);

// set value in TIC namespace
esp_err_t tic_set_value_in_nvs(const char *key, const char *str_type, const char *str_value);
