
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "nvs.h"

#include "tic_types.h"
#include "tic_config.h"
#include "tic_console.h"

static const char *TIC_NVS_NAMESPACE = "tic";

// signature  des fonctions ESP-IDF nvs_get_str() et nvs_get_blob()
typedef esp_err_t nvs_get_func_t( nvs_handle_t nvs, const char* key, char *out_buf, size_t *out_len);


typedef struct {
    nvs_type_t type;
    const char *str;
} type_str_pair_t;

static const type_str_pair_t type_str_pair[] = {
    { NVS_TYPE_I8, "i8" },
    { NVS_TYPE_U8, "u8" },
    { NVS_TYPE_U16, "u16" },
    { NVS_TYPE_I16, "i16" },
    { NVS_TYPE_U32, "u32" },
    { NVS_TYPE_I32, "i32" },
    { NVS_TYPE_U64, "u64" },
    { NVS_TYPE_I64, "i64" },
    { NVS_TYPE_STR, "str" },
    { NVS_TYPE_BLOB, "blob" },
    { NVS_TYPE_ANY, "any" },
};

static const size_t TYPE_STR_PAIR_SIZE = sizeof(type_str_pair) / sizeof(type_str_pair[0]);
static const char *TAG = "nvs_utils.c";

nvs_type_t str_to_nvstype(const char *type)
{
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (strcmp(type, p->str) == 0) {
            return  p->type;
        }
    }

    return NVS_TYPE_ANY;
}

const char *nvstype_to_str(nvs_type_t type)
{
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (p->type == type) {
            return  p->str;
        }
    }

    return "Unknown";
}

static esp_err_t store_blob(nvs_handle_t nvs, const char *key, const char *str_values)
{
    uint8_t value;
    size_t str_len = strlen(str_values);
    size_t blob_len = str_len / 2;

    if (str_len % 2) {
        ESP_LOGE(TAG, "Blob data must contain even number of characters");
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    char *blob = (char *)malloc(blob_len);
    if (blob == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0, j = 0; i < str_len; i++) {
        char ch = str_values[i];
        if (ch >= '0' && ch <= '9') {
            value = ch - '0';
        } else if (ch >= 'A' && ch <= 'F') {
            value = ch - 'A' + 10;
        } else if (ch >= 'a' && ch <= 'f') {
            value = ch - 'a' + 10;
        } else {
            ESP_LOGE(TAG, "Blob data contain invalid character");
            free(blob);
            return ESP_ERR_NVS_TYPE_MISMATCH;
        }

        if (i & 1) {
            blob[j++] += value;
        } else {
            blob[j] = value << 4;
        }
    }

    esp_err_t err = nvs_set_blob(nvs, key, blob, blob_len);
    free(blob);

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    return err;
}

static void print_blob(const char *blob, size_t len)
{
    for (int i = 0; i < len; i++) {
        printf("%02x", blob[i]);
    }
    printf("\n");
}




// alloue un buffer qui doit être libéré par l'appelant 
static tic_error_t get_blob_or_string( nvs_get_func_t *f, nvs_handle_t nvs, const char* key, char **out_buf, size_t *out_len)
{
    assert (*out_buf == NULL);  // le buffer est alloué par la fonction et non par l'appelant
    esp_err_t err;
    size_t len;
    if (out_len) { *out_len = 0; }

    if ( (err = f(nvs, key, NULL, &len)) == ESP_OK) 
    {
        *out_buf = (char *)malloc(len);   // a liberer par l'appelant
        if( !out_buf )
        {
            ESP_LOGE( TAG, "malloc() failed");
            return TIC_ERR_OUT_OF_MEMORY;
        }
        
        if ( (err = f(nvs, key, *out_buf, &len)) == ESP_OK) {
            //printf("%s=%s\n", key, *out_buf);
            if (out_len) { *out_len = len; }
            return TIC_OK;
        }
        free(*out_buf);      // libere le buffer en cas d'erreur
        *out_buf = NULL;
    }
    ESP_LOGD( TAG, "erreur nvs_get_blob() %02x", err );
    return TIC_ERR_NVS;
}

// alloue un buffer qui doit être libéré par l'appelant
tic_error_t console_nvs_get_string( const char* key, char **out_buf )
{
    nvs_handle_t nvs;
    tic_error_t err;

    err = nvs_open(TIC_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return TIC_ERR_NVS;
    }

   err = get_blob_or_string(&nvs_get_str, nvs, key, out_buf, NULL);
   nvs_close(nvs);
   return err;
}


// alloue un buffer qui doit être libéré par l'appelant
tic_error_t console_nvs_get_blob( const char* key, char **out_buf, size_t *out_len )
{
    nvs_handle_t nvs;
    tic_error_t err; 

    err = nvs_open(TIC_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return TIC_ERR_NVS;
    }

   err = get_blob_or_string((nvs_get_func_t *)(&nvs_get_blob), nvs, key, out_buf, out_len);
   nvs_close(nvs);
   return err;
}


// alloue un buffer qui doit être libéré par l'appelant 
tic_error_t console_nvs_get_blob_as_string( const char* key, char **out_buf )
{
    tic_error_t err;
    char *blob = NULL;     // alloué par console_nvs_get_blob(), à liberer dans cette fonction.
    size_t len;

    err = console_nvs_get_blob( key, &blob, &len);
    if (err != TIC_OK )
    {
        return err;
    }

    *out_buf = calloc(1, 2*(len+1));     // a liberer par l'appelant
    if( !(*out_buf) )
    {
        ESP_LOGE( TAG, "malloc() failed");
        err = TIC_ERR_OUT_OF_MEMORY;
    }
    else
    {
        for (int i = 0; i < len; i++)
        {
            snprintf( &((*out_buf)[2*i]), 4, "%02x", blob[i] );  //taille 4 car on a alloué 2*(len+1)
        }
        //err = TIC_OK;
    }
    free(blob);
    return err;
}


esp_err_t set_value_in_nvs(const char *namespace, const char *key, const char *str_type, const char *str_value)
{
    esp_err_t err;
    nvs_handle_t nvs;
    bool range_error = false;

    nvs_type_t type = str_to_nvstype(str_type);

    if (type == NVS_TYPE_ANY) {
        ESP_LOGE(TAG, "Type '%s' is undefined", str_type);
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    err = nvs_open(namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    if (type == NVS_TYPE_I8) {
        int32_t value = strtol(str_value, NULL, 0);
        if (value < INT8_MIN || value > INT8_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_i8(nvs, key, (int8_t)value);
        }
    } else if (type == NVS_TYPE_U8) {
        uint32_t value = strtoul(str_value, NULL, 0);
        if (value > UINT8_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_u8(nvs, key, (uint8_t)value);
        }
    } else if (type == NVS_TYPE_I16) {
        int32_t value = strtol(str_value, NULL, 0);
        if (value < INT16_MIN || value > INT16_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_i16(nvs, key, (int16_t)value);
        }
    } else if (type == NVS_TYPE_U16) {
        uint32_t value = strtoul(str_value, NULL, 0);
        if (value > UINT16_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_u16(nvs, key, (uint16_t)value);
        }
    } else if (type == NVS_TYPE_I32) {
        int32_t value = strtol(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_i32(nvs, key, value);
        }
    } else if (type == NVS_TYPE_U32) {
        uint32_t value = strtoul(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_u32(nvs, key, value);
        }
    } else if (type == NVS_TYPE_I64) {
        int64_t value = strtoll(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_i64(nvs, key, value);
        }
    } else if (type == NVS_TYPE_U64) {
        uint64_t value = strtoull(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_u64(nvs, key, value);
        }
    } else if (type == NVS_TYPE_STR) {
        err = nvs_set_str(nvs, key, str_value);
    } else if (type == NVS_TYPE_BLOB) {
        err = store_blob(nvs, key, str_value);
    }

    if (range_error || errno == ERANGE) {
        nvs_close(nvs);
        return ESP_ERR_NVS_VALUE_TOO_LONG;
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Value stored under key '%s'", key);
        }
    }

    nvs_close(nvs);
    return err;
}

esp_err_t tic_set_value_in_nvs(const char *key, const char *str_type, const char *str_value)
{
    return set_value_in_nvs(TIC_NVS_NAMESPACE, key, str_type, str_value);
}


esp_err_t print_value_from_nvs(const char *namespace, const char *key, const char *str_type)
{
    nvs_handle_t nvs;
    esp_err_t err;

    nvs_type_t type = str_to_nvstype(str_type);

    if (type == NVS_TYPE_ANY) {
        ESP_LOGE(TAG, "Type '%s' is undefined", str_type);
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    err = nvs_open(namespace, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    if (type == NVS_TYPE_I8) {
        int8_t value;
        err = nvs_get_i8(nvs, key, &value);
        if (err == ESP_OK) {
            printf("%d\n", value);
        }
    } else if (type == NVS_TYPE_U8) {
        uint8_t value;
        err = nvs_get_u8(nvs, key, &value);
        if (err == ESP_OK) {
            printf("%u\n", value);
        }
    } else if (type == NVS_TYPE_I16) {
        int16_t value;
        err = nvs_get_i16(nvs, key, &value);
        if (err == ESP_OK) {
            printf("%u\n", value);
        }
    } else if (type == NVS_TYPE_U16) {
        uint16_t value;
        if ((err = nvs_get_u16(nvs, key, &value)) == ESP_OK) {
            printf("%u\n", value);
        }
    } else if (type == NVS_TYPE_I32) {
        int32_t value;
        if ((err = nvs_get_i32(nvs, key, &value)) == ESP_OK) {
            printf("%"PRIi32"\n", value);
        }
    } else if (type == NVS_TYPE_U32) {
        uint32_t value;
        if ((err = nvs_get_u32(nvs, key, &value)) == ESP_OK) {
            printf("%"PRIu32"\n", value);
        }
    } else if (type == NVS_TYPE_I64) {
        int64_t value;
        if ((err = nvs_get_i64(nvs, key, &value)) == ESP_OK) {
            printf("%lld\n", value);
        }
    } else if (type == NVS_TYPE_U64) {
        uint64_t value;
        if ( (err = nvs_get_u64(nvs, key, &value)) == ESP_OK) {
            printf("%llu\n", value);
        }
    } else if (type == NVS_TYPE_STR) {
        size_t len;
        if ( (err = nvs_get_str(nvs, key, NULL, &len)) == ESP_OK) {
            char *str = (char *)malloc(len);
            if ( (err = nvs_get_str(nvs, key, str, &len)) == ESP_OK) {
                printf("%s\n", str);
            }
            free(str);
        }
    } else if (type == NVS_TYPE_BLOB) {
        size_t len;
        if ( (err = nvs_get_blob(nvs, key, NULL, &len)) == ESP_OK) {
            char *blob = (char *)malloc(len);
            if ( (err = nvs_get_blob(nvs, key, blob, &len)) == ESP_OK) {
                print_blob(blob, len);
            }
            free(blob);
        }
    }

    nvs_close(nvs);
    return err;
}
