

#define WIFI_RECONNECT_LOOP_DELAY   10000      // ms

void wifi_task_start();
void wifi_reconnect();


tic_error_t wifi_scan_start(int timeout_sec);
