class SunPosition {
private:
  double latitude;
  double longitude;

  /**
     * 將角度轉換為弧度
     * @param degrees 角度值
     * @return 弧度值
     */
  inline double toRadians(double degrees) {
    return degrees * M_PI / 180.0;
  }

  /**
     * 將弧度轉換為角度
     * @param radians 弧度值
     * @return 角度值
     */
  inline double toDegrees(double radians) {
    return radians * 180.0 / M_PI;
  }

public:
  /**
     * 建構函數，初始化太陽位置計算器
     * @param lat 緯度
     * @param lon 經度
     */
  SunPosition(double lat, double lon)
    : latitude(lat), longitude(lon) {}

  /**
     * 計算指定時間的太陽方位角和仰角
     * @param timeinfo 時間結構指針
     * @param azimuth 返回計算的方位角
     * @param elevation 返回計算的仰角
     */
  void calculatePosition(struct tm* timeinfo, double& azimuth, double& elevation) {
    // 計算日期相關數據
    int year = timeinfo->tm_year + 1900;
    int month = timeinfo->tm_mon + 1;
    int day = timeinfo->tm_mday;
    int hour = timeinfo->tm_hour;
    int minute = timeinfo->tm_min;
    int second = timeinfo->tm_sec;

    // 輸出當前時間用於調試
    Serial.printf("計算太陽位置: %d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);

    // 計算當天的小時數（包含分和秒的小數部分）
    double hourDecimal = hour + minute / 60.0 + second / 3600.0;

    // 計算當年的天數
    int dayOfYear = 0;
    for (int m = 1; m < month; m++) {
      if (m == 2) {
        dayOfYear += (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
      } else if (m == 4 || m == 6 || m == 9 || m == 11) {
        dayOfYear += 30;
      } else {
        dayOfYear += 31;
      }
    }
    dayOfYear += day;

    Serial.printf("一年中的第%d天, 小時數: %.3f\n", dayOfYear, hourDecimal);

    // 計算太陽時角
    double gamma = 2 * M_PI / 365 * (dayOfYear - 1 + (hourDecimal - 12) / 24);
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));

    // 計算赤緯角
    double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
                  - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
                  - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
    decl = toDegrees(decl);

    Serial.printf("方程式時間: %.3f, 赤緯角: %.3f°\n", eqtime, decl);

    // 計算真太陽時
    double time_offset = eqtime - 4 * longitude + 60 * 8;  // 東八區，調整為您的時區
    double tst = hourDecimal * 60 + time_offset;

    // 計算太陽時角
    double ha = (tst / 4 - 180);  // 每分鐘轉動0.25度
    if (ha < -180) ha += 360;

    Serial.printf("真太陽時: %.3f, 太陽時角: %.3f°\n", tst, ha);

    // 計算太陽高度角
    double lat_rad = toRadians(latitude);
    double decl_rad = toRadians(decl);
    double ha_rad = toRadians(ha);

    double sin_elev = sin(lat_rad) * sin(decl_rad) + cos(lat_rad) * cos(decl_rad) * cos(ha_rad);
    elevation = toDegrees(asin(sin_elev));

    // 計算方位角
    double cos_az = (sin(decl_rad) - sin(lat_rad) * sin_elev) / (cos(lat_rad) * cos(toRadians(elevation)));
    cos_az = constrain(cos_az, -1.0, 1.0);  // 確保在有效範圍內

    azimuth = toDegrees(acos(cos_az));
    if (ha > 0) azimuth = 360 - azimuth;

    Serial.printf("計算結果 - 太陽方位角: %.3f°, 高度角: %.3f°\n", azimuth, elevation);
  }

  /**
     * 使用二分法查找明天的日出時間及其太陽位置
     * @param timeinfo 當前時間結構指針
     * @param azimuth 返回日出時的方位角
     * @param elevation 返回日出時的仰角
     * @return 是否成功找到日出時間
     */
  bool findSunriseTime(struct tm* timeinfo, double& azimuth, double& elevation) {
    // 創建臨時時間結構，設置為明天凌晨
    struct tm tomorrowMorning = *timeinfo;
    tomorrowMorning.tm_mday += 1;
    tomorrowMorning.tm_hour = 4;  // 從凌晨4點開始搜索
    tomorrowMorning.tm_min = 0;
    tomorrowMorning.tm_sec = 0;

    time_t startTime = mktime(&tomorrowMorning);
    if (startTime == -1) return false;

    // 設置結束時間為明天 8 點
    tomorrowMorning.tm_hour = 8;
    time_t endTime = mktime(&tomorrowMorning);
    if (endTime == -1) return false;

    // 二分法搜索日出時間
    while (endTime - startTime > 60) {  // 精確到1分鐘
      time_t midTime = startTime + (endTime - startTime) / 2;
      struct tm midTimeinfo;
      localtime_r(&midTime, &midTimeinfo);

      double midAzimuth, midElevation;
      calculatePosition(&midTimeinfo, midAzimuth, midElevation);

      if (midElevation < 0) {
        startTime = midTime;
      } else {
        endTime = midTime;
      }
    }

    // 使用找到的時間計算最終太陽位置
    struct tm sunriseTimeinfo;
    localtime_r(&endTime, &sunriseTimeinfo);
    calculatePosition(&sunriseTimeinfo, azimuth, elevation);

    // 輸出找到的日出時間
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &sunriseTimeinfo);
    Serial.print("Calculated sunrise time: ");
    Serial.println(timeString);

    return true;
  }
};
