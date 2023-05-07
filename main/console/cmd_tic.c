
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "nvs.h"

#include "tic_config.h"
#include "tic_console.h"
#include "nvs_utils.h"
#include "wifi.h"       // pour forcer une reconnexion wifi
#include "mqtt.h"       // pour forcer une reconnexion mqtt


static const char *TAG = "cmd_tic.c";


#define SCAN_TIMEOUT_SEC    (15)

static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_set_args;


static struct {
    struct arg_str *uri;
    struct arg_end *end;
} broker_args;


static struct {
    struct arg_str *id;
    struct arg_str *psk;
    struct arg_end *end;
} mqtt_psk_args;


static struct {
    struct arg_int *timeout;
    struct arg_end *end;
} scan_args;

static int wifi_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &wifi_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_set_args.end, argv[0]);
        return TIC_ERR_CONSOLE_BAD_CMD;
    }

    const char *ssid = wifi_set_args.ssid->sval[0];
    const char *password = wifi_set_args.password->sval[0];

    esp_err_t err;

    // TODO : erreur si le SSID est vide
    err = tic_set_value_in_nvs( TIC_NVS_WIFI_SSID, "str", ssid );
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "erreur d'enregistrement NVS de la clé %s : %s", TIC_NVS_WIFI_SSID, esp_err_to_name(err));
        return TIC_ERR_NVS;
    }

    // TODO : warning si le mot de passe est vide
    err = tic_set_value_in_nvs( TIC_NVS_WIFI_PASSWORD, "str", password );
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "erreur d'enregistrement NVS de la clé %s : %s", TIC_NVS_WIFI_PASSWORD, esp_err_to_name(err));
        return TIC_ERR_NVS;
    }

    wifi_reconnect();
    return TIC_OK;

    // TODO: relancer le wifi avec ces nouveaux credentials
}


static int wifi_scan(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &scan_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, scan_args.end, argv[0]);
        return 1;
    }
   
    /* set default values*/
    if (scan_args.timeout->count == 0) {
        scan_args.timeout->ival[0] = SCAN_TIMEOUT_SEC;
    }

    return (wifi_scan_start( scan_args.timeout->ival[0] ) != TIC_OK);
}


static int mqtt_broker_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &broker_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, broker_args.end, argv[0]);
        return TIC_ERR_CONSOLE_BAD_CMD;
    }

    const char *broker = broker_args.uri->sval[0];
    esp_err_t err;

    // TODO : erreur si le SSID est vide
    err = tic_set_value_in_nvs(TIC_NVS_MQTT_BROKER, "str", broker );
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "erreur d'enregistrement NVS de la clé %s : %s", TIC_NVS_MQTT_BROKER, esp_err_to_name(err));
        return TIC_ERR_NVS;
    }
    return mqtt_client_restart();
}


static int mqtt_psk_set(int argc, char **argv)
{

    int nerrors = arg_parse(argc, argv, (void **) &mqtt_psk_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqtt_psk_args.end, argv[0]);
        return TIC_ERR_CONSOLE_BAD_CMD;
    }

    const char *identity = mqtt_psk_args.id->sval[0];
    const char *psk = mqtt_psk_args.psk->sval[0];

    esp_err_t err;

    // TODO : erreur si le SSID est vide
    err = tic_set_value_in_nvs(TIC_NVS_MQTT_PSK_ID, "str", identity );
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "erreur d'enregistrement NVS de la clé %s : %s", TIC_NVS_MQTT_PSK_ID, esp_err_to_name(err));
        return TIC_ERR_NVS;
    }

    // TODO : warning si le mot de passe est vide
    err = tic_set_value_in_nvs( TIC_NVS_MQTT_PSK_KEY, "blob", psk );
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "erreur d'enregistrement NVS de la clé %s : %s", TIC_NVS_MQTT_PSK_KEY, esp_err_to_name(err));
        return TIC_ERR_NVS;
    }

    return mqtt_client_restart();
}



static int show_params(int argc, char**argv)
{
    char missing[] = "<aucun>";
    char *ssid     = NULL;
    char *password = NULL;
    char *broker   = NULL;
    char *id       = NULL;
    char *psk      = NULL;

    // ignore errors
    console_nvs_get_string(TIC_NVS_WIFI_SSID, &ssid );
    console_nvs_get_string(TIC_NVS_WIFI_PASSWORD, &password);
    console_nvs_get_string(TIC_NVS_MQTT_BROKER, &broker);
    console_nvs_get_string(TIC_NVS_MQTT_PSK_ID, &id);
    console_nvs_get_blob_as_string(TIC_NVS_MQTT_PSK_KEY, &psk);

    ssid = ssid ? ssid : missing;    
    password = password ? password : missing;
    broker = broker ? broker : missing;
    id = id ? id : missing;
    psk = psk ? psk : missing;

    printf("Wifi SSID='%s' password='%s'\n", ssid, password );
    printf("Mqtt broker='%s'\n", broker );
    printf("Mqtt PSK id='%s' key='%s'\n", id, psk);
    printf("free heap size %"PRIu32"\n", esp_get_free_heap_size());

    if (ssid != missing) free(ssid);
    if (password != missing) free(password);
    if (broker != missing) free(broker);
    if (id != missing) free(id);
    if (psk != missing) free(psk);
    
    return 0;
}


static void register_wifi_scan(void)
{
    scan_args.timeout = arg_int0("t", "timeout", "<t>", "Duree max du scan (s)");
    scan_args.end = arg_end(2);

    const esp_console_cmd_t scan_cmd = {
        .command = "wifi_scan",
        .help = "Scan wifi APs",
        .hint = NULL,
        .func = &wifi_scan,
        .argtable = &scan_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&scan_cmd) );
}


static void register_wifi_set(void)
{
    wifi_set_args.ssid = arg_str1(NULL, NULL, "<ssid>", NULL);
    wifi_set_args.password = arg_str0(NULL, NULL, "<pwd>", NULL);
    wifi_set_args.end = arg_end(2);

    const esp_console_cmd_t wifi_set_cmd = {
        .command = "wifi_set",
        .help = "Parametres wifi\n",
        .hint = NULL,
        .func = &wifi_set,
        .argtable = &wifi_set_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_set_cmd));
}

static void register_mqtt_broker_set(void)
{
    broker_args.uri = arg_str1(NULL, NULL, "<url>", "url of the MQTT broker");
    broker_args.end = arg_end(2);

    const esp_console_cmd_t mqtt_cmd = {
        .command = "mqtt_broker_set",
        .help = "Adresse URI du broker MQTT. Exemple 'mqtts://mqtt.broker.net:8883'\n",
        .hint = NULL,
        .func = &mqtt_broker_set,
        .argtable = &broker_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_cmd));
}

static void register_mqtt_psk_set(void)
{
    mqtt_psk_args.id = arg_str1(NULL, NULL, "<id>", "Identité associée à la clé secrete");
    mqtt_psk_args.psk = arg_str1(NULL, NULL, "<psk>", "Clé secrete en hexadecimal sans 0x. Ex: 'deadbeef51'");
    mqtt_psk_args.end = arg_end(2);

    const esp_console_cmd_t mqtt_cmd = {
        .command = "mqtt_psk_set",
        .help = "Preshared key MQTT\n",
        .hint = NULL,
        .func = &mqtt_psk_set,
        .argtable = &mqtt_psk_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_cmd));
}


static void register_show_params(void)
{
    const esp_console_cmd_t status_cmd = {
        .command = "show_params",
        .help = "Voir les parametres définis en NVS\n",
        .hint = NULL,
        .func = &show_params,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));
}


void console_register_commands(void)
{
    register_show_params();
    register_wifi_set();
    register_wifi_scan();
    register_mqtt_broker_set();
    register_mqtt_psk_set();
}
