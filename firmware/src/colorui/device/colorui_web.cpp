// Wi-Fi file manager for the SD card. See colorui_web.h.
//
// Everything here runs in the ColorUI task (core 0), the same task that draws
// the screen, so SD and LCD traffic on the shared SPI3 bus never overlap. The
// RNode's own Wi-Fi link to Reticulum (port 7633) is untouched.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "colorui_web.h"
#include "colorui_web_page.h"
#include "../core/cui.h"

#ifndef COLORUI_VERSION
#define COLORUI_VERSION "0.7"
#endif

extern bool colorui_sd_live();     // colorui_device.cpp
void colorui_ota_progress(int pct, const char* msg);    // colorui_device.cpp
void colorui_store_fw_hash(const uint8_t* hash32);      // RNode_Firmware.ino

namespace {

WebServer* srv = nullptr;
File up_file;
bool up_ok = false, up_auth = false;
String up_path;

bool authed() {
  const char* pw = cui::settings().web_password;
  if (!pw[0]) return true;
  if (srv->authenticate("rnode", pw)) return true;
  srv->requestAuthentication(BASIC_AUTH, "RNode R8 ColorUI (user: rnode)");
  return false;
}

bool safePath(const String& p) {
  return p.length() > 0 && p.length() < 160 && p[0] == '/' && p.indexOf("..") < 0 && p.indexOf("//") < 0 &&
         p.indexOf('\\') < 0;
}

void mkParents(const String& path) {
  int i = 1;
  while ((i = path.indexOf('/', i)) > 0) {
    const String d = path.substring(0, i);
    if (!SD.exists(d)) SD.mkdir(d);
    ++i;
  }
}

String jsonEsc(const char* s) {
  String o;
  for (; *s; ++s) {
    const char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

const char* mimeOf(const String& p) {
  if (p.endsWith(".csv")) return "text/csv";
  if (p.endsWith(".txt") || p.endsWith(".ini")) return "text/plain; charset=utf-8";
  if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
  if (p.endsWith(".png")) return "image/png";
  if (p.endsWith(".bmp")) return "image/bmp";
  return "application/octet-stream";
}

void hRoot() {
  if (!authed()) return;
  srv->send_P(200, "text/html", kWebPage);
}

void hLs() {
  if (!authed()) return;
  String dir = srv->hasArg("dir") ? srv->arg("dir") : "/";
  if (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);
  if (!safePath(dir)) { srv->send(400, "text/plain", "bad path"); return; }
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) { srv->send(404, "text/plain", "no such folder"); return; }
  String j = "{\"brand\":\"";
  j += jsonEsc(cui::settings().brand[0] ? cui::settings().brand : "RNode R8 ColorUI");
  j += "\",\"ver\":\"ColorUI " COLORUI_VERSION "\",\"total\":";
  j += String((double)SD.totalBytes(), 0);
  j += ",\"used\":";
  j += String((double)SD.usedBytes(), 0);
  j += ",\"items\":[";
  bool first = true;
  int n = 0;
  for (File f = d.openNextFile(); f && n < 500; f = d.openNextFile(), ++n) {
    const char* nm = f.name();
    const char* base = strrchr(nm, '/');
    base = base ? base + 1 : nm;
    if (!strcmp(base, "System Volume Information")) { f.close(); continue; }
    if (!first) j += ',';
    first = false;
    j += "{\"n\":\""; j += jsonEsc(base); j += "\",\"d\":"; j += f.isDirectory() ? "1" : "0";
    j += ",\"s\":"; j += String((unsigned long)f.size()); j += '}';
    f.close();
    if ((n & 15) == 15) vTaskDelay(1);
  }
  d.close();
  j += "]}";
  srv->send(200, "application/json", j);
}

void hGet() {
  if (!authed()) return;
  const String p = srv->arg("path");
  if (!safePath(p)) { srv->send(400, "text/plain", "bad path"); return; }
  File f = SD.open(p);
  if (!f || f.isDirectory()) { srv->send(404, "text/plain", "not found"); return; }
  if (srv->hasArg("dl")) {
    const int s = p.lastIndexOf('/');
    srv->sendHeader("Content-Disposition", "attachment; filename=\"" + p.substring(s + 1) + "\"");
  }
  srv->streamFile(f, mimeOf(p));
  f.close();
}

// multipart upload: the destination comes from ?path=, not the form's file name
void hUploadData() {
  HTTPUpload& u = srv->upload();
  if (u.status == UPLOAD_FILE_START) {
    up_ok = false;
    up_path = srv->arg("path");
    const char* pw = cui::settings().web_password;
    up_auth = !pw[0] || srv->authenticate("rnode", pw);
    if (!up_auth || !safePath(up_path) || up_path.endsWith("/")) return;
    mkParents(up_path);
    if (SD.exists(up_path)) SD.remove(up_path);
    up_file = SD.open(up_path, FILE_WRITE);
    up_ok = (bool)up_file;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (up_ok && up_file.write(u.buf, u.currentSize) != u.currentSize) up_ok = false;
    vTaskDelay(1);                                   // let core 0's idle task run
  } else if (u.status == UPLOAD_FILE_END) {
    if (up_file) up_file.close();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (up_file) up_file.close();
    if (up_ok) SD.remove(up_path);
    up_ok = false;
  }
}

void hUploadDone() {
  if (!authed()) return;
  srv->send(up_ok ? 200 : 500, "text/plain", up_ok ? "ok" : "upload failed");
}

// ---- firmware update over Wi-Fi ----
// The new image goes into the other app slot; when it is complete and checks
// out, its SHA-256 is registered as the RNode's firmware hash (what
// rnode_sethash.py does over USB) and the board restarts into it.
bool ota_ok = false, ota_auth = false;
String ota_err;
void hOtaData() {
  HTTPUpload& u = srv->upload();
  if (u.status == UPLOAD_FILE_START) {
    const char* pw = cui::settings().web_password;
    ota_auth = !pw[0] || srv->authenticate("rnode", pw);
    ota_ok = false; ota_err = "";
    if (!ota_auth || !cui::settings().web_update) { ota_err = "not allowed"; return; }
    colorui_ota_progress(0, "Updating firmware");
    ota_ok = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!ota_ok) ota_err = Update.errorString();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (ota_ok && Update.write(u.buf, u.currentSize) != u.currentSize) { ota_ok = false; ota_err = Update.errorString(); }
    const size_t len = srv->clientContentLength();
    if (len) colorui_ota_progress((int)(u.totalSize * 100 / len), "Updating firmware");
    vTaskDelay(1);
  } else if (u.status == UPLOAD_FILE_END) {
    if (ota_ok && !Update.end(true)) { ota_ok = false; ota_err = Update.errorString(); }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort(); ota_ok = false; ota_err = "upload aborted";
  }
}
void hOtaDone() {
  if (!authed()) return;
  if (!ota_ok) {
    colorui_ota_progress(0, "Update failed");
    srv->send(500, "text/plain", "update failed: " + ota_err);
    delay(1500);
    return;
  }
  const esp_partition_t* p = esp_ota_get_boot_partition();
  uint8_t h[32];
  if (!p || esp_partition_get_sha256(p, h) != ESP_OK) { srv->send(500, "text/plain", "could not hash the new image"); return; }
  colorui_ota_progress(100, "Done - restarting");
  srv->send(200, "text/plain", "ok - restarting into the new firmware");
  delay(500);
  colorui_store_fw_hash(h);            // writes the RNode's target hash
  delay(300);
  ESP.restart();
}

void hRm() {
  if (!authed()) return;
  const String p = srv->arg("path");
  if (!safePath(p) || p == "/") { srv->send(400, "text/plain", "bad path"); return; }
  File f = SD.open(p);
  if (!f) { srv->send(404, "text/plain", "not found"); return; }
  const bool dir = f.isDirectory();
  f.close();
  const bool ok = dir ? SD.rmdir(p) : SD.remove(p);
  srv->send(ok ? 200 : 409, "text/plain", ok ? "ok" : (dir ? "folder not empty" : "failed"));
}

void hMkdir() {
  if (!authed()) return;
  const String p = srv->arg("path");
  if (!safePath(p)) { srv->send(400, "text/plain", "bad path"); return; }
  mkParents(p + "/");
  srv->send(SD.exists(p) ? 200 : 500, "text/plain", "ok");
}

void hRestart() {
  if (!authed()) return;
  srv->send(200, "text/plain", "restarting");
  delay(400);
  ESP.restart();
}

}  // namespace

void colorui_web_poll() {
  static bool started = false;
  static uint32_t last_try = 0;
  if (!started) {
    if (!cui::settings().web || !colorui_sd_live()) return;
    if (millis() - last_try < 5000) return;
    last_try = millis();
    if (WiFi.status() != WL_CONNECTED) return;
    srv = new WebServer(80);
    srv->on("/", HTTP_GET, hRoot);
    srv->on("/api/ls", HTTP_GET, hLs);
    srv->on("/api/get", HTTP_GET, hGet);
    srv->on("/api/upload", HTTP_POST, hUploadDone, hUploadData);
    srv->on("/api/rm", HTTP_POST, hRm);
    srv->on("/api/mkdir", HTTP_POST, hMkdir);
    srv->on("/api/restart", HTTP_POST, hRestart);
    srv->on("/api/ota", HTTP_POST, hOtaDone, hOtaData);
    srv->onNotFound([] { srv->send(404, "text/plain", "not found"); });
    srv->begin();
    if (MDNS.begin("rnode-r8")) MDNS.addService("http", "tcp", 80);
    started = true;
    const String ip = WiFi.localIP().toString();
    char url[40];
    snprintf(url, sizeof url, "http://%s", ip.c_str());
    cui::setWebUrl(url);
  }
  srv->handleClient();
}
