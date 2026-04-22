#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>

// Dados da SUA rede (Roteador)
const char* local_ssid = "Archenar";
const char* local_pass = "PASS1234567890000";

// Dados do AP do Cardputer (Rede Própria)
const char* ap_ssid = "Cardputer_NAS";
const char* ap_pass = "123456789";

WebServer server(80);
File fsUploadFile;

String formatBytes(size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else return String(bytes / 1024.0 / 1024.0) + " MB";
}

void handleRoot() {
  String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{background:#1a1a1a; color:#00ff41; font-family:monospace; padding:20px;} ";
  html += "table{width:100%; border-collapse:collapse; margin-top:20px;} th,td{text-align:left; padding:10px; border-bottom:1px solid #333;} ";
  html += "a{color:#00ffff; text-decoration:none;} .del{color:#ff4444;} .box{border:1px solid #00ff41; padding:15px;}</style></head><body>";
  html += "<h1>[ Cardputer NAS ]</h1>";
  html += "<div class='box'><h3>Upload</h3><form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='f'><input type='submit' value='Enviar'></form></div>";
  html += "<table><tr><th>Arquivo</th><th>Tamanho</th><th>Ações</th></tr>";

  File root = SD.open("/");
  File file = root.openNextFile();
  while(file){
    if (!file.isDirectory()) {
      String n = String(file.name());
      html += "<tr><td>" + n + "</td><td>" + formatBytes(file.size()) + "</td>";
      html += "<td>";

      // Se for HTML, mostra o link de "Abrir"
      if (n.endsWith(".html") || n.endsWith(".htm") || n.endsWith(".png") 
         || n.endsWith(".mp4") || n.endsWith(".pdf") || n.endsWith(".mp3")) {
          html += "<a href='/view?file=" + n + "' target='_blank'>[ Abrir ]</a> ";
      }

      html += "<a href='/download?file=" + n + "'>Download</a> | ";
      html += "<a class='del' href='/delete?file=" + n + "'>Excluir</a></td></tr>";
    }
    file = root.openNextFile();
  }
  html += "</table></body></html>";
  server.send(200, "text/html", html);
}

// --- LÓGICA DE ARQUIVOS (Download/Delete/Upload iguais ao anterior) ---
void handleDelete() {
  String fileName = "/" + server.arg("file");
  if (SD.remove(fileName)) { server.sendHeader("Location", "/"); server.send(303); }
}

void handleDownload() {
  String fileName = server.arg("file");
  String path = "/" + fileName;
  
  File file = SD.open(path, FILE_READ);
  if (file) {
    // Esse é o segredo: diz ao navegador o nome exato do arquivo
    server.sendHeader("Content-Disposition", "attachment; filename=" + fileName);
    server.streamFile(file, "application/octet-stream");
    file.close();
  } else {
    server.send(404, "text/plain", "Arquivo nao encontrado");
  }
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    
    // Se o arquivo já existe, deletamos para sobrescrever
    if (SD.exists(filename)) SD.remove(filename);
    
    fsUploadFile = SD.open(filename, FILE_WRITE);
    Serial.print("Upload iniciado: "); Serial.println(filename);
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      Serial.println("Upload finalizado com sucesso.");
    } else {
      Serial.println("Erro: Arquivo nao estava aberto.");
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (fsUploadFile) fsUploadFile.close();
    Serial.println("Upload abortado pelo usuário.");
  }
}

void handleView() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Arquivo nao especificado");
    return;
  }

  String fileName = server.arg("file");
  String path = "/" + fileName;

  if (SD.exists(path)) {
    File file = SD.open(path, FILE_READ);
    String contentType = "text/plain"; // Padrão

    // Identifica se é HTML, CSS ou JS para o navegador renderizar certo
    if (fileName.endsWith(".html") || fileName.endsWith(".htm")) contentType = "text/html";
    else if (fileName.endsWith(".css")) contentType = "text/css";
    else if (fileName.endsWith(".js")) contentType = "application/javascript";
    else if (fileName.endsWith(".png")) contentType = "image/png";
    else if (fileName.endsWith(".jpg")) contentType = "image/jpeg";
    else if (fileName.endsWith(".pdf")) contentType = "application/pdf";

    server.streamFile(file, contentType);
    file.close();
  } else {
    server.send(404, "text/plain", "Arquivo nao encontrado");
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setRotation(1);

  // Inicialização do SD com seu pino 12 mestre
  SPI.begin(40, 39, 14, 12); 
  if (!SD.begin(12, SPI, 20000000)) {
    M5.Lcd.println("Erro no SD!");
    return;
  }

  // CONFIGURAÇÃO HÍBRIDA (AP + STATION)
  WiFi.mode(WIFI_AP_STA);
  
  // Conecta na sua rede local
  WiFi.begin(local_ssid, local_pass);
  
  // Cria o Access Point próprio
  WiFi.softAP(ap_ssid, ap_pass);

  // Rotas
  server.on("/view", HTTP_GET, handleView);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/delete", HTTP_GET, handleDelete);

  // Rota de Upload no setup()
  server.on("/upload", HTTP_POST, []() {
    // Esta parte roda DEPOIS que o upload termina ou falha
    // Enviamos um HTML com um Timer de segurança para voltar à home
    String html = "<html><head><meta http-equiv='refresh' content='2;url=/'></head>";
    html += "<body style='background:#1a1a1a; color:#00ff41; font-family:monospace; text-align:center;'>";
    html += "<h2>Processando Arquivo...</h2>";
    html += "<p>Retornando em 2 segundos...</p>";
    html += "<script>setTimeout(function(){window.location.href='/';}, 1500);</script>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }, handleFileUpload);

  server.begin();

  // Interface no LCD
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.println("NAS ATIVO");
  M5.Lcd.println("----------------");
  M5.Lcd.setTextColor(CYAN);
  M5.Lcd.print("AP IP: "); M5.Lcd.println(WiFi.softAPIP().toString());
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.print("STA IP: "); 
  
  // Espera um pouco para mostrar o IP da sua rede
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED && counter < 10) {
    delay(500);
    M5.Lcd.print(".");
    counter++;
  }
  M5.Lcd.println(WiFi.localIP().toString());
}

void loop() {
  server.handleClient();
  M5.update();
}