# ESP32 WiFi Checker

購入兩片具備無線網路功能的單晶片開發板，一片是使用 ESP32 晶片 **ESP32-WROOM-32**，另一片規格較低，是 ESP8266 晶片的 **NodeMCU Lua WI-FI V3**。剛買到手的板子，需要檢測一下無線網路功能是否正常運作，就使用 AI 快速撰寫了這個程式碼。

## 測試程式

### ESP32

程式運作流程為

1. 在 setup() 中嘗試連接 WiFi router 直到成功。
2. 連線至 https://ifconfig.me/ip，如果連線成功，會回傳 WiFi router IP，輸出到序列埠；若失敗則輸出錯誤訊息至序列埠。
3. 等得十秒，重複第二步驟。

### ESP8266

測試 ESP8266 時，需要更改匯入的標頭檔（.h），如下所示。

```c++
1 #include <WiFi.h>        --> 1 #include <ESP8266WiFi.h>
2 #include <HTTPClient.h>  --> 2 #include <ESP8266HTTPClient.h>
```



## 觀察心得

1. 程式上傳開發板的速度，ESP32 明顯比 ESP8266 快得多。
2. ESP8266 開發板有額外 SSL support 選項，可以選擇完整版（All SSL ciphers）或者基本版（Basic SSL ciphers）。推測是因為 ESP8266 儲存空間較小，在必要的時候可以只使用基本版 SSL support。 