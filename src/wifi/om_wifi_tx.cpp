#include "om_wifi_tx.h"
#include <WiFi.h>
#include "om_status_led.h"

extern "C" {
  #include "esp_wifi.h"
}
#include <esp_now.h>

// ==== 你自己的全局（一般放在 .h）====
uint8_t slaveMac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Replace with receiver STA MAC
typedef struct {
  int value;
} DataPacket;

DataPacket data;

static uint32_t g_nextSendMs = 0;
static const uint8_t OM_WIFI_CH = 1;

static void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  om_status_pulse_tx();

  if (status == ESP_NOW_SEND_SUCCESS) {
    om_status_mark_link_alive();                 // ✅ 关键：不然 TX 会黄
    om_status_set_link_state(OM_LINK_CONNECTED); // ✅ 绿呼吸
  } else {
    om_status_pulse_error();
    om_status_set_link_state(OM_LINK_ERROR);
  }
}



static void dump_wifi_state(const char* tag){
  wifi_mode_t m;
  esp_wifi_get_mode(&m);

  uint8_t primary;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);

  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);

  Serial.printf("[%s] mode=%d ch=%u mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
    tag, (int)m, (unsigned)primary,
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}



void om_wifi_init() {
  // 建议只在 main 里 Serial.begin 一次，这里先不管
  // Serial.begin(115200);

  // 1) 进入 STA 模式 + 关 sleep
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.setSleep(false);

  // 2) 再用 IDF 层彻底关省电（双保险，S3 很有用）
  esp_wifi_set_ps(WIFI_PS_NONE);

  // 3) 确保 WiFi 真正 start（这一步经常就是 0x306C 的解药）
  esp_err_t e = esp_wifi_start();
  Serial.printf("[TX] esp_wifi_start: %s\n", esp_err_to_name(e));

  // 4) 固定频道（在 start 后设更稳）
  e = esp_wifi_set_channel(OM_WIFI_CH, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[TX] set_channel: %s\n", esp_err_to_name(e));

  dump_wifi_state("TX");

  // 5) ESPNOW
  e = esp_now_init();
  Serial.printf("[TX] esp_now_init: %s\n", esp_err_to_name(e));
  if (e != ESP_OK) return;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveMac, 6);
  peerInfo.channel = OM_WIFI_CH;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;     // ✅ 必设：绑定 STA 接口

  e = esp_now_add_peer(&peerInfo);
  Serial.printf("[TX] add_peer: %s\n", esp_err_to_name(e));
  if (e != ESP_OK) return;

  Serial.println("[TX] ESP-NOW ready");
}

void om_wifi_service() {
  // 非阻塞定时：1s 发一次
  uint32_t now = millis();
  if ((int32_t)(now - g_nextSendMs) < 0) return;
  g_nextSendMs = now + 1000;

  data.value = random(0, 100);


    esp_err_t err = esp_now_send(slaveMac, (uint8_t*)&data, sizeof(data));
    if (err != ESP_OK) {
    Serial.printf("[TX] send err=%d (0x%04X) %s\n",
                    (int)err, (unsigned)err, esp_err_to_name(err));
    }
}
