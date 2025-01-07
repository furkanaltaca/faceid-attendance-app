#include "esp_camera.h"
#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "fr_flash.h"
#include "esp_http_server.h"

// Select camera model
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// const char* ssid = "Turksat_Kablonet_2.4_NEuK";
// const char* password = "23Tinker";

const char* ssid = "Furkan iPhone";
const char* password = "123456789FA";

void startCameraServer();
extern face_id_list id_list;
extern int8_t detection_enabled;
extern int8_t recognition_enabled;
// void streamTask(void *pvParameters);

#define TOUCH_PIN 12
unsigned long buttonPressStartTime = 0;
const unsigned long holdDuration = 2000;
bool buttonHandled = false;
bool buttonWasPressed = false;

struct Student {
  String name;
  int studentNo;
  int faceId;
  bool isPresent;
};
#define MAX_STUDENTS 10
Student students[MAX_STUDENTS];
int studentCount = 0;
int currentStudentIndex = -1;
int faceId = -1;
boolean isEnrollMode = false;
boolean isEnrollingFace = false;
String lastDisplayedUserText = "";
String currentLcdTextLine1 = "";
String currentLcdTextLine2 = "";

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  setupCamera();

  fetchStudentList();

  printSerial("Yoklama Modu", "Hoşgeldiniz!");

  // Delete All faces from storage
  // while (delete_face_id_in_flash(&id_list) > -1) {
  //   Serial.println("Deleting Face");
  // }
  // Serial.println("All Deleted");
}

void loop() {
  int touchValue = touchRead(TOUCH_PIN);
  bool buttonPressed = (touchValue == 1);
  if (buttonPressed && !buttonWasPressed) {
    buttonPressStartTime = millis();
    buttonHandled = false;
  } else if (buttonPressed && buttonWasPressed && !buttonHandled) {
    if (millis() - buttonPressStartTime >= holdDuration) {
      isEnrollMode = !isEnrollMode;
      faceId = -1;
      isEnrollingFace = false;

      if (isEnrollMode) {
        Serial.println("Kayit Modu");
        sendModeInfo("Kayıt Modu");
      } else {
        Serial.println("Yoklama Modu");
        sendModeInfo("Yoklama Modu");
      }
      buttonHandled = true;
      delay(100);
    }
  } else if (!buttonPressed && buttonWasPressed) {
    if (!buttonHandled && millis() - buttonPressStartTime < holdDuration) {
      if (isEnrollMode) {
        faceId = -1;
        isEnrollingFace = true;
        nextStudent();
      }
    }
    buttonHandled = true;
    buttonPressStartTime = 0;
  }

  buttonWasPressed = buttonPressed;

  if (isEnrollMode) {
    if (isEnrollingFace == true) {
      enrollmentMode();
    }
  } else {
    attendanceMode();
  }

  delay(500);
}

void enrollmentMode() {
  String currentStudentName = students[currentStudentIndex].name;
  if (students[currentStudentIndex].faceId == -1) {

    if (lastDisplayedUserText != currentStudentName) {
      printSerial("Yuzunuzu okutun", currentStudentName);
      lastDisplayedUserText = currentStudentName;
    }
    int detectedFaceId = detectFace();
    if (detectedFaceId != -1) {
      students[currentStudentIndex].faceId = detectedFaceId;
      sendEnrollFace(students[currentStudentIndex].studentNo, students[currentStudentIndex].faceId);
      printSerial(currentStudentName, "Kayit Tamam!");
      faceId = -1;
      isEnrollingFace = false;
    }
  } else {
    printSerial(currentStudentName, "Kaydedilmiş");
  }
}

void attendanceMode() {
  int detectedFaceId = detectFace();
  if (detectedFaceId != -1) {
    for (int i = 0; i < studentCount; i++) {
      if (students[i].faceId == detectedFaceId) {
        if (!students[i].isPresent) {
          students[i].isPresent = true;
          printSerial("Hosgeldiniz", students[i].name);
          printSerial("Hosgeldiniz", String(students[i].studentNo));
          sendAttendance(students[i].studentNo);
        } else {
          faceId = -1;
        }
        return;
      }
    }
  }
}

int detectFace() {
  if (faceId != -1) {
    Serial.print("Face Id: ");
    Serial.println(faceId);
  }

  return faceId;
}

void nextStudent() {
  Serial.println("Next student for enroll");
  currentStudentIndex++;
  if (currentStudentIndex >= studentCount) {
    currentStudentIndex = 0;
  }

  while (students[currentStudentIndex].faceId != -1) {
    currentStudentIndex++;
    if (currentStudentIndex >= studentCount) {
      currentStudentIndex = 0;
    }
  }

  String currentStudentName = students[currentStudentIndex].name;
  sendNextStudentInfoForEnroll(currentStudentName);
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);
  s->set_gain_ctrl(s, 0);
  s->set_exposure_ctrl(s, 0);
  s->set_brightness(s, 0);
  s->set_saturation(s, 0);
  s->set_hmirror(s, 1);

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("Wi-Fi Sinyal Gücü (RSSI): " + String(WiFi.RSSI()) + " dBm");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  sendCameraWebServerIp("http://" + WiFi.localIP().toString());
}

void printSerial(String line1, String line2) {
  line1 = turkceToAscii(line1);
  line2 = turkceToAscii(line2);
  if (currentLcdTextLine1 != line1 || currentLcdTextLine2 != line2) {
    currentLcdTextLine1 = line1;
    currentLcdTextLine2 = line2;

    Serial.println(line1);
    Serial.println(line2);
  }
}

String turkceToAscii(String input) {
  input.replace("Ç", "C");
  input.replace("Ş", "S");
  input.replace("Ğ", "G");
  input.replace("İ", "I");
  input.replace("Ü", "U");
  input.replace("Ö", "O");
  input.replace("ç", "c");
  input.replace("ş", "s");
  input.replace("ğ", "g");
  input.replace("ı", "i");
  input.replace("ü", "u");
  input.replace("ö", "o");
  return input;
}

void fetchStudentList() {
  disableFaceRecognition();
  delay(1000);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://yoklamasistemi.azurewebsites.net/students");
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
      String payload = http.getString();

      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        JsonArray array = doc.as<JsonArray>();
        studentCount = 0;

        for (JsonVariant value : array) {
          if (studentCount < MAX_STUDENTS) {
            JsonObject obj = value.as<JsonObject>();

            students[studentCount].name = obj["name"].as<String>();
            students[studentCount].studentNo = obj["id"].as<int>();
            students[studentCount].faceId = obj["face_id"].isNull() ? -1 : obj["face_id"].as<int>();
            students[studentCount].isPresent = obj["attended"].as<bool>();

            studentCount++;
          } else {
            Serial.println("Maksimum öğrenci sayısına ulaşıldı!");
            break;
          }
        }
        Serial.println("Toplam öğrenci sayısı: " + String(studentCount));
      } else {
        Serial.println("JSON ayrıştırma hatası: " + String(error.c_str()));
      }
    } else {
      Serial.println("HTTP isteği başarısız! Kod: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("Wi-Fi bağlantısı yok!");
  }

  delay(1000);
  enableFaceRecognition();
}

void sendAttendance(int studentNo) {
  disableFaceRecognition();
  delay(1000);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://yoklamasistemi.azurewebsites.net/enrollForLesson");

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "*/*");

    String httpRequestData = "{\"StudentNo\":" + String(studentNo) + "}";

    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0) {
      Serial.printf("[http] POST... code: %d\n", httpResponseCode);
      if (httpResponseCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Yanıt: " + response);
      }
    } else {
      Serial.printf("[http] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  }
  delay(1000);
  enableFaceRecognition();
}

void sendEnrollFace(int studentNo, int faceId) {
  disableFaceRecognition();
  delay(1000);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://yoklamasistemi.azurewebsites.net/updateFaceID");

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "*/*");

    String httpRequestData = "{\"StudentNo\":" + String(studentNo) + ", \"FaceID\":" + String(faceId) + "}";

    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0) {
      Serial.printf("[http] POST... code: %d\n", httpResponseCode);
      if (httpResponseCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Yanıt: " + response);
      }
    } else {
      Serial.printf("[http] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  }
  delay(1000);
  enableFaceRecognition();
}

void sendCameraWebServerIp(String ipAddress) {
  disableFaceRecognition();
  delay(1000);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://yoklamasistemi.azurewebsites.net/SaveCameraServerIpAddress");

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "*/*");

    String httpRequestData = "{\"IpAddress\":\"" + String(ipAddress) + ":81/stream\"}";

    Serial.println(httpRequestData);

    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0) {
      Serial.printf("[http] POST... code: %d\n", httpResponseCode);
      if (httpResponseCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Yanıt: " + response);
      }
    } else {
      Serial.printf("[http] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  }
  delay(1000);
  enableFaceRecognition();
}

void sendModeInfo(String mode) {
  disableFaceRecognition();
  delay(1000);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://yoklamasistemi.azurewebsites.net/modeInfo");

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "*/*");

    String httpRequestData = "{\"Mode\":\"" + mode + "\"}";

    Serial.println(httpRequestData);

    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0) {
      Serial.printf("[http] POST... code: %d\n", httpResponseCode);
      if (httpResponseCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Yanıt: " + response);
      }
    } else {
      Serial.printf("[http] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  }
  delay(1000);
  enableFaceRecognition();
}

void sendNextStudentInfoForEnroll(String studentName) {
  disableFaceRecognition();
  delay(1000);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://yoklamasistemi.azurewebsites.net/nextStudentInfo");

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "*/*");

    String httpRequestData = "{\"StudentName\":\"" + studentName + "\"}";

    Serial.println(httpRequestData);

    int httpResponseCode = http.POST(httpRequestData);

    if (httpResponseCode > 0) {
      Serial.printf("[http] POST... code: %d\n", httpResponseCode);
      if (httpResponseCode == 201) {
        String response = http.getString();
        Serial.println("Yanıt: " + response);
      }
    } else {
      Serial.printf("[http] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  }
  delay(1000);
  enableFaceRecognition();
}

void disableFaceRecognition() {
  detection_enabled = 0;
  recognition_enabled = 0;
  faceId = -1;
}

void enableFaceRecognition() {
  detection_enabled = 1;
  recognition_enabled = 1;
}
